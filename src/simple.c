#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>

#include "simple.h"
#include "lib/log.h"
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

static int _simple_check_ino (struct simple_fs *fs, fuse_ino_t ino) {
    return (ino < 1 || ino > fs->inode_count) ? EIO : 0;
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
    
    // make sure ino is valid
    if ((err = _simple_check_ino(fs, ino)))
        ERROR("invalid inode");

    // look up the node
    node = fs->inode_table + (ino - 1);

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


/*
 * Define our fuse_lowlevel_ops struct.
 */
static struct fuse_lowlevel_ops simple_ops = {
    .lookup = simple_lookup,

    .getattr = simple_getattr,
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
