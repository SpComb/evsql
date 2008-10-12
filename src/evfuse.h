#ifndef EVFUSE_H
#define EVFUSE_H

#define FUSE_USE_VERSION 26

#include <event2/event.h>
#include <fuse/fuse_lowlevel.h>

/*
 * A wrapper for the fuse + libevent context
 */
struct evfuse;

/*
 * Create a new new evfuse context.
 */
struct evfuse *evfuse_new (struct event_base *evbase, struct fuse_args *args, struct fuse_lowlevel_ops *llops, void *cb_data);

/*
 * Close and free evfuse context.
 *
 * Safe to call after errors/llops.destroy
 */
void evfuse_free (struct evfuse *ctx);

#endif /* EVFUSE_H */

