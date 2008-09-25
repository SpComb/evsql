
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "evfuse.h"
#include "lib/common.h"

struct evfuse {
    // the /dev/fuse fd/channel that we get from fuse_mount
    struct fuse_chan *chan;

    // the session that we use to process the fuse stuff
    struct fuse_session *session;

    // the event that we use to receive requests
    struct event *ev;
    
    // what our receive-message length is
    size_t recv_size;

    // the buffer that we use to receive events
    char *recv_buf;
};

static void _evfuse_ev_read (evutil_socket_t fd, short what, void *arg) {
    struct evfuse *ctx = arg;
    struct fuse_chan *ch = ctx->chan;
    int res;
    
    // loop until we complete a recv
    do {
        // a new fuse_req is available
        res = fuse_chan_recv(&ch, ctx->recv_buf, ctx->recv_size);
    } while (res == -EINTR);

    if (res == 0)
        ERROR("fuse_chan_recv gave EOF");

    if (res < 0 && res != -EAGAIN)
        ERROR("fuse_chan_recv failed: %s", strerror(-res));
    
    if (res > 0) {
        INFO("[evfuse] got %d bytes from /dev/fuse", res);

        // received a fuse_req, so process it
        fuse_session_process(ctx->session, ctx->recv_buf, res, ch);
    }
    
    // reschedule
    if (event_add(ctx->ev, NULL))
        PERROR("event_add");

    // ok, wait for the next event
    return;

error:
    // XXX: fail
    FATAL("no error handling yet...");
}

struct evfuse *evfuse_new (struct event_base *evbase, struct fuse_args *args, struct fuse_lowlevel_ops *llops, void *cb_data) {
    struct evfuse *ctx = NULL;
    char *mountpoint;
    int multithreaded, foreground;
    
    // allocate our context
    if ((ctx = calloc(1, sizeof(*ctx))) == NULL)
        ERROR("calloc");

    // parse the commandline for the mountpoint
    if (fuse_parse_cmdline(args, &mountpoint, &multithreaded, &foreground) == -1)
        ERROR("fuse_parse_cmdline");

    // mount it
    if ((ctx->chan = fuse_mount(mountpoint, args)) == NULL)
        PERROR("fuse_mount_common");

    // the receive buffer stufff
    ctx->recv_size = fuse_chan_bufsize(ctx->chan);

    // allocate the recv buffer
    if ((ctx->recv_buf = malloc(ctx->recv_size)) == NULL)
        ERROR("malloc");
    
    // allocate a low-level session
    if ((ctx->session = fuse_lowlevel_new(args, llops, sizeof(*llops), cb_data)) == NULL)
        PERROR("fuse_lowlevel_new");
    
    // add the channel to the session
    // this isn't strictly necessary, but let's do it anyways
    fuse_session_add_chan(ctx->session, ctx->chan);
    
    // now, we can start listening for events on the channel
    if ((ctx->ev = event_new(evbase, fuse_chan_fd(ctx->chan), EV_READ, &_evfuse_ev_read, ctx)) == NULL)
        ERROR("event_new");

    if (event_add(ctx->ev, NULL))
        PERROR("event_add");

    // and then we wait
    return ctx;

error:
    free(ctx);

    return NULL;
}