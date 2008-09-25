#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <event2/event.h>
#include <fuse/fuse_opt.h>

#include "lib/common.h"
#include "lib/math.h"
#include "evfuse.h"

const char *file_name = "hello";
const char *file_data = "Hello World\n";

static struct hello {
    struct event_base *ev_base;

    struct evfuse *ev_fuse;

} ctx;

void hello_init (void *userdata, struct fuse_conn_info *conn) {
    INFO("[hello.init] userdata=%p, conn=%p", userdata, conn);
}

void hello_destroy (void *userdata) {
    INFO("[hello.destroy] userdata=%p", userdata);
}

void hello_lookup (fuse_req_t req, fuse_ino_t parent, const char *name) {
    struct fuse_entry_param e;

    INFO("[hello.lookup] (uid=%d, pid=%d) parent=%lu name=%s", fuse_req_ctx(req)->uid, fuse_req_ctx(req)->pid, parent, name);

    // the world is flat
    if (parent != 1 || strcmp(name, file_name)) {
        fuse_reply_err(req, ENOENT);

        return;
    }
    
    // set up the entry
    memset(&e, 0, sizeof(e));
    e.ino = 2;
    e.attr_timeout = 1.0;
    e.entry_timeout = 1.0;
    e.attr.st_mode = S_IFREG | 0444;
    e.attr.st_nlink = 1;
    e.attr.st_size = strlen(file_data);

    // reply
    fuse_reply_entry(req, &e);
}

void hello_getattr (fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
    struct stat stbuf;

    INFO("[hello.getattr] (uid=%d, pid=%d) ino=%lu, fi=%p", fuse_req_ctx(req)->uid, fuse_req_ctx(req)->pid, ino, fi);

    memset(&stbuf, 0, sizeof(stbuf));
    
    // the root dir, or the file?
    if (ino == 1) {
        stbuf.st_mode = S_IFDIR | 0555;
        stbuf.st_nlink = 2;

    } else if (ino == 2) {
        stbuf.st_mode = S_IFREG | 0444;
        stbuf.st_nlink = 1;
        stbuf.st_size = strlen(file_data);

    } else {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    // reply
    fuse_reply_attr(req, &stbuf, 1.0);
}

struct dirbuf {
    char *buf;
    size_t len;
    size_t off;
};

#define DIRBUF_INITIAL_SIZE 1024

static int dirbuf_init (struct dirbuf *buf) {
    buf->len = DIRBUF_INITIAL_SIZE;
    buf->off = 0;
    
    // allocate the mem
    if ((buf->buf = malloc(buf->len)) == NULL)
        ERROR("malloc");
    
    // ok
    return 0;

error:
    return -1;
}

/*
 * Ensure that `new` bytes fit into the buf. If they already fit, update offset and set *retry = 0. If they don't fit,
 * grow buf and set *retry = 1.
 *
 * Returns 0 on success, -1 on failure (don't retry).
 */
static int dirbuf_update (struct dirbuf *buf, size_t new, int *retry) {
    if (buf->off + new <= buf->len) {
        INFO("\thello.dirbuf_update: update offset by %zu from %zu -> %zu", new, buf->off, buf->off + new);

        // great, it fit, update offset and return
        buf->off += new;

        *retry = 0;

    } else {
        size_t old_len = buf->len;

        // calc new size
        do {
            buf->len *= 2;

        } while (buf->off + new > buf->len);

        INFO("\thello.dirbuf_update: grow size for %zu from %zu -> %zu", new, old_len, buf->len);
        
        // realloc
        if ((buf->buf = realloc(buf->buf, buf->len)) == NULL)
            ERROR("realloc");

        // done, just retry
        *retry = 1;
    }
    
    // success
    return 0;

error:
    return -1;
}

static int dirbuf_add (fuse_req_t req, size_t req_size, off_t req_off, struct dirbuf *buf, off_t ent_off, off_t next_off, const char *ent_name, fuse_ino_t ent_ino) {
    struct stat stbuf;
    size_t ent_size;
    int err, retry;

    INFO("\thello.dirbuf_add: req_size=%zu, req_off=%zu, buf->len=%zu, buf->off=%zu, ent_off=%zu, next_off=%zu, ent_name=%s, ent_ino=%lu",
        req_size, req_off, buf->len, buf->off, ent_off, next_off, ent_name, ent_ino);

    // skip entries as needed
    if (buf->len >= req_size || ent_off < req_off) 
        return 0;

    // set ino
    stbuf.st_ino = ent_ino;

    // add the dirent and update dirbuf until it fits
    do {
        ent_size = fuse_add_direntry(req, buf->buf + buf->off, buf->len - buf->off, ent_name, &stbuf, next_off);

    } while (!(err = dirbuf_update(buf, ent_size, &retry)) && retry);

    if (err)
        return err;

    // success
    return 0;
}

static int dirbuf_done (fuse_req_t req, struct dirbuf *buf, size_t req_size) {
    int err;
    
    // send the reply, return the error later
    err = fuse_reply_buf(req, buf->buf, MIN(buf->off, req_size));

    INFO("\thello.dirbuf_done: MIN(%zu, %zu)=%zu, err=%d", buf->off, req_size, MIN(buf->off, req_size), err);

    // free the dirbuf
    free(buf->buf);

    // return the error code
    return err;
}

void hello_readdir (fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi) {
    int err = 0;
    struct dirbuf buf;

    INFO("[hello.readdir] ino=%lu, size=%zu, off=%zu, fi=%p", ino, size, off, fi);

    // there exists only one dir
    if (ino != 1) {
        fuse_reply_err(req, ENOTDIR);
        return;
    }

    // fill in the dirbuf
    if (dirbuf_init(&buf))
        ERROR("failed to init dirbuf");

    if (    dirbuf_add(req, size, off, &buf, 0, 1,  ".",        1)
        ||  dirbuf_add(req, size, off, &buf, 1, 2,  "..",       1)
        ||  dirbuf_add(req, size, off, &buf, 2, 3,  file_name,  2)
    ) ERROR("failed to add dirents to buf");
    
    // send it
    if ((err = -dirbuf_done(req, &buf, size)))
        EERROR(-err, "failed to send buf");

    // success
    return;

error:
    if ((err = fuse_reply_err(req, err ? err : EIO)))
        EWARNING(err, "failed to send error reply");
}

struct fuse_lowlevel_ops hello_llops = {
    .init = &hello_init,
    .destroy = &hello_destroy,

    .lookup = &hello_lookup,
    .getattr = &hello_getattr,

    .readdir = &hello_readdir,
};


int main (int argc, char **argv) {
    struct fuse_args fuse_args = FUSE_ARGS_INIT(argc, argv);

    // init libevent
    if ((ctx.ev_base = event_base_new()) == NULL)
        FATAL("event_base_new");
    
    // open fuse
    if ((ctx.ev_fuse = evfuse_new(ctx.ev_base, &fuse_args, &hello_llops, &ctx)) == NULL)
        FATAL("evfuse_new");

    // run libevent
    INFO("running libevent loop");

    if (event_base_dispatch(ctx.ev_base))
        PWARNING("event_base_dispatch");

    // cleanup
    event_base_free(ctx.ev_base);
}

