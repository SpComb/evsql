#ifndef LIB_SIGNAL_H
#define LIB_SIGNAL_H

/*
 * Handle signals in a libevent-sane way
 */

#include <event2/event.h>

/*
 * How many signals we can define actions for
 */
#define MAX_SIGNALS 8

/*
 * info about a set of signals
 */
struct signals;

/*
 * Used as a handler for signals that should cause a loopexit.
 */
void signals_loopexit (int signal, short event, void *arg);

/*
 * Used to receive signals, but discard them.
 */
void signals_ignore (int signal, short event, void *arg);

/*
 * Allocate a signals struct, acting on the given ev_base.
 *
 * Returns NULL on failure
 */
struct signals *signals_alloc (struct event_base *ev_base);

/*
 * Add a signal to be handled by the given signals struct with the given handler.
 */
int signals_add (struct signals *signals, int sigval, void (*sig_handler)(evutil_socket_t, short, void *));

/*
 * Add a set of default signals
 *      SIGPIPE     signals_ignore
 *      SIGINT      signals_loopexit
 */
struct signals *signals_default (struct event_base *ev_base);

/*
 * Free the resources/handlers associated with the given signal handler
 */
void signals_free (struct signals *signals);

#endif /* LIB_SIGNAL_H */
