#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>

#include "simple.h"
#include "dirbuf.h"
#include "lib/log.h"
#include "lib/math.h"
#include "lib/misc.h"

struct simple_fs {
    const struct simple_node *inode_table;

    size_t inode_count;
};

/*
 * Used for stat/entry timeouts... not sure how this should really be set.
 */
#define CACHE_TIMEOUT 1.0

static void _simple_stat (struct stat *stat, const struct simple_node *node) {
    stat->st_ino = node->inode;
    stat->st_mode = node->mode_type | node->mode_perm;
    stat->st_nlink = 1;
    stat->st_size = node->data ? strlen(node->data) : 0;
}

/*
 * Fetch the simple_node for the given inode.
 *
 * Returns NULL for invalid inodes.
 */
static const struct simple_node *_simple_get_ino (struct simple_fs *fs, fuse_ino_t ino) {
    // make sure it's a valid inode
    if (ino < 1 || ino > fs->inode_count) {
        WARNING("invalid inode=%zu", ino);
        return NULL;
    }
    
    // return the node
    return fs->inode_table + (ino - 1);
}

static void simple_lookup (fuse_req_t req, fuse_ino_t parent, const char *name) {
    struct simple_fs *fs = fuse_req_userdata(req);
    const struct simple_node *node;
    struct fuse_entry_param e; ZINIT(e);
    int err;
    
    INFO("[simple.lookup %p] parent=%lu, name=`%s'", fs, parent, name);

    // find the matching node
    for (node = fs->inode_table; node->inode > 0; node++) {
        if (node->parent == parent && strcmp(node->name, name) == 0)
            break;

    }

    // did we find it?
    if (node->inode) {
        // set up the entry
        e.ino = node->inode;
        e.generation = 0x01;
        _simple_stat(&e.attr, node);
        e.attr_timeout = CACHE_TIMEOUT;
        e.entry_timeout = CACHE_TIMEOUT;

        // reply
        if ((err = fuse_reply_entry(req, &e)))
            EERROR(err, "fuse_reply_entry");

    } else {
        // not found
        err = ENOENT;
        goto error;
    }

    // success
    return;

error:
    if ((err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");
}

static void simple_getattr (fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
    struct simple_fs *fs = fuse_req_userdata(req);
    const struct simple_node *node;
    struct stat stbuf; ZINIT(stbuf);
    int err;

    INFO("[simple.getattr %p] ino=%lu", fs, ino);
    
    // look up the node 
    if ((node = _simple_get_ino(fs, ino)) == NULL)
        EERROR(err = EINVAL, "bad inode");
    
    // set up the stbuf
    _simple_stat(&stbuf, node);
    
    // reply
    if ((err = fuse_reply_attr(req, &stbuf, CACHE_TIMEOUT)))
        EERROR(err, "fuse_reply_attr");
    
    // suceccss
    return;

error:
    if ((err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");
}

static void simple_readlink (fuse_req_t req, fuse_ino_t ino) {
    struct simple_fs *fs = fuse_req_userdata(req);
    const struct simple_node *node;
    int err;

    INFO("[simple.readlink %p] ino=%lu", fs, ino);
    
    // look up the node 
    if ((node = _simple_get_ino(fs, ino)) == NULL)
        EERROR(err = EINVAL, "bad inode");

    // check that it's a symlink
    if (node->mode_type != S_IFLNK)
        EERROR(err = EINVAL, "bad mode");

    // return the contents
    if ((err = fuse_reply_readlink(req, node->data)))
        EERROR(err, "fuse_reply_readlink");

    // suceccss
    return;

error:
    if ((err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");

}

static void simple_readdir (fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi) {
    struct simple_fs *fs = fuse_req_userdata(req);
    const struct simple_node *dir_node, *node;
    struct dirbuf buf;
    int err;

    INFO("[simple.readdir] ino=%lu, size=%zu, off=%zu, fi=%p", ino, size, off, fi);
    
    // look up the inode
    if ((dir_node = _simple_get_ino(fs, ino)) == NULL)
        EERROR(err = EINVAL, "bad inode");
    
    // check that it's a dir
    if (dir_node->mode_type != S_IFDIR)
        EERROR(err = ENOTDIR, "bad mode");

    // fill in the dirbuf
    if (dirbuf_init(&buf, size, off))
        ERROR("failed to init dirbuf");
    
    // add . and ..
    // we set the next offset to 2, because all dirent offsets will be larger than that
    err =   dirbuf_add(req, &buf, 0, 1, ".",   dir_node->inode,    S_IFDIR )
        ||  dirbuf_add(req, &buf, 1, 2, "..",  dir_node->inode,    S_IFDIR );
    
    if (err != 0)
        EERROR(err, "failed to add . and .. dirents");

    // look up all child nodes
    for (node = fs->inode_table; node->inode; node++) {
        // skip non-children
        if (node->parent != dir_node->inode)
            continue;
        
        // child node offsets are just inode + 2
        if ((err = dirbuf_add(req, &buf, node->inode + 2, node->inode + 3, node->name, node->inode, node->mode_type)) < 0)
            EERROR(err, "failed to add dirent for inode=%lu", node->inode);
        
        // stop if it's full
        if (err > 0)
            break;
    }

    // send it
    if ((err = -dirbuf_done(req, &buf)))
        EERROR(err, "failed to send buf");

    // success
    return;

error:
    if ((err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");
}

static void simple_read (fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi) {
    struct simple_fs *fs = fuse_req_userdata(req);
    const struct simple_node *node;
    int err ;

    // fi is unused
    (void) fi;

    INFO("[simple.read] ino=%lu, size=%zu, off=%zu, fi=%p", ino, size, off, fi);
    
    // look up the inode
    if ((node = _simple_get_ino(fs, ino)) == NULL)
        EERROR(err = EINVAL, "bad inode");
    
    // check that it's a dir
    if (node->mode_type != S_IFREG)
        EERROR(err = (node->mode_type == S_IFDIR ? EISDIR : EINVAL), "bad mode");
    
    // seek past EOF?
    if (off >= strlen(node->data)) {
        // offset is out-of-file, so return EOF
        if ((err = fuse_reply_buf(req, NULL, 0)))
            EERROR(err, "fuse_reply_buf size=0");

    } else {
        // reply with the requested file data
        if ((err = fuse_reply_buf(req, node->data + off, MIN(strlen(node->data) - off, size))))
            EERROR(err, "fuse_reply_buf buf=%p + %zu, size=MIN(%zu, %zu)", node->data, off, strlen(node->data) - off, size);
    }

    // success
    return;

error:
    if ((err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");
}


/*
 * Define our fuse_lowlevel_ops struct.
 */
static struct fuse_lowlevel_ops simple_ops = {
    .lookup = simple_lookup,

    .getattr = simple_getattr,

    .readlink = simple_readlink,

    .readdir = simple_readdir,

    .read = simple_read,
};

struct fuse_lowlevel_ops *simple_init () {
    return &simple_ops;
}

struct simple_fs *simple_new (const struct simple_node *node_list) {
    struct simple_fs *fs = NULL;
    const struct simple_node *node;
    
    // generate
    if ((fs = calloc(1, sizeof(*fs))) == NULL)
        ERROR("calloc");

    // remember node_list
    fs->inode_count = 0;
    fs->inode_table = node_list;
    
    // validate it
    for (node = fs->inode_table; node->inode; node++) {
        // update inode_count
        fs->inode_count++;

        // check that parent is valid
        assert(node->inode == fs->inode_count);
        assert(node->parent < node->inode);
    }
    
    // success
    return fs;

error:
    return NULL;
}
