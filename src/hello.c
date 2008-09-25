#include <event2/event.h>
#include <fuse/fuse_opt.h>

#include "lib/common.h"
#include "evfuse.h"

struct hello {
    struct event_base *ev_base;

    struct evfuse *ev_fuse;
} ctx;

void hello_init (void *userdata, struct fuse_conn_info *conn) {
    INFO("[hello.init] userdata=%p, conn=%p", userdata, conn);
}

void hello_destroy (void *userdata) {
    INFO("[hello.destroy] userdata=%p", userdata);
}

struct fuse_lowlevel_ops hello_llops = {
    .init = &hello_init,
    .destroy = &hello_destroy,
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

