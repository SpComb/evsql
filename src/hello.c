#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <event2/event.h>
#include <fuse/fuse_opt.h>

#include "lib/log.h"
#include "lib/math.h"
#include "lib/signals.h"
#include "evfuse.h"
#include "dirbuf.h"

const char *file_name = "hello";
const char *file_data = "Hello World\n";

static struct hello {
    struct event_base *ev_base;

    struct signals *signals;

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
    if (dirbuf_init(&buf, size, off))
        ERROR("failed to init dirbuf");

    err =   dirbuf_add(req, &buf, 0, 1,  ".",        1,    S_IFDIR )
        ||  dirbuf_add(req, &buf, 1, 2,  "..",       1,    S_IFDIR )
        ||  dirbuf_add(req, &buf, 2, 3,  file_name,  2,    S_IFREG );

    if (err < 0)
        ERROR("failed to add dirents to buf");
    
    // send it
    if ((err = -dirbuf_done(req, &buf)))
        EERROR(-err, "failed to send buf");

    // success
    return;

error:
    if ((err = fuse_reply_err(req, err ? err : EIO)))
        EWARNING(err, "failed to send error reply");
}

void hello_open (fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
    int err = 0;

    INFO("[hello.open] ino=%lu, fi=%p, fi->flags=%08X", ino, fi, fi->flags);

    if (ino != 2) {
        // must open our only file, not the dir
        fuse_reply_err(req, ino == 1 ? EISDIR : ENOENT);
        return;

    } else if ((fi->flags & 0x03) != O_RDONLY) {
        // "permission denied"
        fuse_reply_err(req, EACCES);
        return;
    }

    // XXX: update fi stuff?

    // open it!
    if ((err = fuse_reply_open(req, fi)))
        EERROR(err, "fuse_reply_open");

    // success
    return;

error:
    if ((err = fuse_reply_err(req, err ? err : EIO)))
        EWARNING(err, "failed to send error reply");
}

void hello_read (fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi) {
    int err = 0;

    // fi is unused
    (void) fi;

    INFO("[hello.read] ino=%lu, size=%zu, off=%zu, fi=%p", ino, size, off, fi);

    if (ino != 2) {
        // EEK!
        FATAL("wrong inode");
    }
    
    if (off >= strlen(file_data)) {
        // offset is out-of-file, so return EOF
        err = fuse_reply_buf(req, NULL, 0);

    } else {
        // reply with the requested file data
        err = fuse_reply_buf(req, file_data + off, MIN(strlen(file_data) - off, size));
    }

    // reply
    if (err)
        PERROR("fuse_reply_buf");
    
    // success
    return;

error:
    if ((err = fuse_reply_err(req, err ? err : EIO)))
        EWARNING(err, "failed to send error reply");
}

void hello_getxattr (fuse_req_t req, fuse_ino_t ino, const char *name, size_t size) {
    INFO("[hello.getxattr] ino=%lu, name=`%s', size=%zu", ino, name, size);

    fuse_reply_err(req, ENOSYS);
}

struct fuse_lowlevel_ops hello_llops = {
    .init = &hello_init,
    .destroy = &hello_destroy,

    .lookup = &hello_lookup,
    .getattr = &hello_getattr,

    .open = &hello_open,

    .read = &hello_read,

    .readdir = &hello_readdir,

    .getxattr = hello_getxattr,
};


int main (int argc, char **argv) {
    struct fuse_args fuse_args = FUSE_ARGS_INIT(argc, argv);
    
    // zero
    memset(&ctx, 0, sizeof(ctx));

    // init libevent
    if ((ctx.ev_base = event_base_new()) == NULL)
        ERROR("event_base_new");
    
    // setup signals
    if ((ctx.signals = signals_default(ctx.ev_base)) == NULL)
        ERROR("signals_default");

    // open fuse
    if ((ctx.ev_fuse = evfuse_new(ctx.ev_base, &fuse_args, &hello_llops, &ctx)) == NULL)
        ERROR("evfuse_new");

    // run libevent
    INFO("running libevent loop");

    if (event_base_dispatch(ctx.ev_base))
        PERROR("event_base_dispatch");
    
    // clean shutdown

error :
    // cleanup
    if (ctx.ev_fuse)
        evfuse_free(ctx.ev_fuse);
    
    if (ctx.signals)
        signals_free(ctx.signals);

    if (ctx.ev_base)
        event_base_free(ctx.ev_base);
    
    fuse_opt_free_args(&fuse_args);
}

