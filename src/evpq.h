#ifndef EVPQ_H
#define EVPQ_H

/*
 * Convenience functions for using libpq (the PostgreSQL client library) with libevent.
 */

#include <event2/event.h>
#include <postgresql/libpq-fe.h>

/*
 * Our PGconn context wrapper.
 */
struct evpq_conn;

/*
 * Callback functions used
 */
struct evpq_callback_info {
    /*
     * This evpq_conn has connected succesfully \o/
     */
    void (*fn_connected)(struct evpq_conn *conn, void *arg);

    /*
     * Got a result.
     */
    void (*fn_result)(struct evpq_conn *conn, PGresult *result, void *arg);

    /*
     * No more results for the query
     */
    void (*fn_done)(struct evpq_conn *conn, void *arg);
    
    /*
     * Something caused this evpq_conn to fail :(
     *
     * XXX: add a `what` arg?
     */
    void (*fn_failure)(struct evpq_conn *conn, void *arg);

    /*
     * Arg to pass through to all callbacks.
     */
    void *cb_arg;
};

/*
 * evpq_conn states
 */
enum evpq_state {
    EVPQ_INIT,

    EVPQ_CONNECT,
    EVPQ_CONNECTED,

    EVPQ_QUERY,

    EVPQ_FAILURE,
};

/*
 * Create a new evpq connection.
 *
 * This corresponds directly to PQconnectStart, and handles all the libevent setup/polling needed.
 *
 * The connection will initially be in the EVPQ_CONNECT state, and will then either callback via fn_connected
 * (EVPQ_CONNECTED) or fn_failure (EVPQ_FAILURE).
 *
 * cb_info contains the callback functions (and the user argument) to use.
 */
struct evpq_conn *evpq_connect (struct event_base *ev_base, const char *conninfo, const struct evpq_callback_info cb_info);

/*
 * Execute a query.
 *
 * This corresponds directly to PQsendQuery. This evpq must be in the EVPQ_CONNECTED state, so you must wait after
 * calling evpq_connect, and you may not run two queries at the same time.
 *
 * The query will result in a series of fn_result (EVPQ_RESULT) calls (if multiple queries in the query string),
 * followed by a fn_done (EVPQ_CONNECTED).
 */
int evpq_query (struct evpq_conn *conn, const char *command);

/*
 * Get the actual PGconn.
 *
 * This can safely be used to access all of the normal PQ functions.
 */
const PGconn *evpq_pgconn (struct evpq_conn *conn);

// convenience wrappers
#define evpq_error_message(conn) PQerrorMessage(evpq_pgconn(conn))


#endif /* EVPQ_H */
