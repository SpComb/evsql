#ifndef EVFUSE_H
#define EVFUSE_H

#include <event2/event.h>
#include <fuse_lowlevel.h>

/*
 * A wrapper for the fuse + libevent context
 */
struct evfuse;

/*
 * Create a new new evfuse context.
 */
struct evfuse *evfuse_new (struct event_base *evbase, struct fuse_args *args, struct fuse_lowlevel_ops *llops, void *cb_data);

#ENDIf /* EVFUSE_H */
