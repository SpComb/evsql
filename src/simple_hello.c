
#include <event2/event.h>

#include "lib/log.h"
#include "lib/signals.h"
#include "evfuse.h"
#include "simple.h"

static struct hello {
    struct event_base *ev_base;

    struct signals *signals;

    struct simple_fs *fs;

    struct evfuse *ev_fuse;

} ctx;

static struct simple_node node_list[] = {
    {   1,  S_IFDIR,    0555,   0,  NULL,       NULL                },
    {   2,  S_IFREG,    0444,   1,  "hello",    "Hello World!\n"    },
    {   0,  0,          0,      0,  NULL,       NULL                },
};

int main (int argc, char **argv) {
    struct fuse_args fuse_args = FUSE_ARGS_INIT(argc, argv);
    
    // init libevent
    if ((ctx.ev_base = event_base_new()) == NULL)
        ERROR("event_base_new");
    
    // setup signals
    if ((ctx.signals = signals_default(ctx.ev_base)) == NULL)
        ERROR("signals_default");
    
    // setup fs
    if ((ctx.fs = simple_new(node_list)) == NULL)
        ERROR("simple_new");

    // open fuse
    if ((ctx.ev_fuse = evfuse_new(ctx.ev_base, &fuse_args, simple_init(), ctx.fs)) == NULL)
        ERROR("evfuse_new");

    // run libevent
    INFO("running libevent loop");

    if (event_base_dispatch(ctx.ev_base))
        PERROR("event_base_dispatch");
    
    // clean shutdown

error :
    // cleanup
    if (ctx.ev_fuse)
        evfuse_close(ctx.ev_fuse);

/*
    if (ctx.fs)
        simple_close(ctx.fs);
*/

    if (ctx.signals)
        signals_free(ctx.signals);

    if (ctx.ev_base)
        event_base_free(ctx.ev_base);
    
    fuse_opt_free_args(&fuse_args);
}

