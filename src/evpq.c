
#include <event2/event.h>
#include <assert.h>
#include <stdlib.h>

#include "evpq.h"
#include "lib/error.h"

struct evpq_conn {
    struct event_base *ev_base;
    struct evpq_callback_info user_cb;
    void *user_cb_arg;

    PGconn *pg_conn;

    struct event *ev;

    enum evpq_state state;
};

/*
 * This evpq_conn has experienced a GENERAL FAILURE.
 */
static void _evpq_failure (struct evpq_conn *conn) {
    // update state
    conn->state = EVPQ_FAILURE;

    // notify
    conn->user_cb.fn_failure(conn, conn->user_cb_arg);
}

/*
 * Initial connect was succesfull
 */
static void _evpq_connect_ok (struct evpq_conn *conn) {
    // update state
    conn->state = EVPQ_CONNECTED;

    // notify
    conn->user_cb.fn_connected(conn, conn->user_cb_arg);
}

/*
 * Initial connect failed
 */
static void _evpq_connect_fail (struct evpq_conn *conn) {
    // just mark it as a generic failure
    _evpq_failure(conn);
}

/*
 * Receive a result and gives it to the user. If there was no more results, update state and tell the user.
 *
 * Returns zero if we got a result, 1 if there were/are no more results to handle.
 */
static int _evpq_query_result (struct evpq_conn *conn) {
    PGresult *result;
    
    // get the result
    if ((result = PQgetResult(conn->pg_conn)) == NULL) {
        // no more results, update state
        conn->state = EVPQ_CONNECTED;

        // tell the user the query is done
        conn->user_cb.fn_done(conn, conn->user_cb_arg);

        // stop waiting for more results
        return 1;

    } else {
        // got a result, give it to the user
        conn->user_cb.fn_result(conn, result, conn->user_cb_arg);

        // great
        return 0;
    }
}

/*
 * Schedule a new _evpq_event for this connection.
 */ 
static int _evpq_schedule (struct evpq_conn *conn, short what, void (*handler)(evutil_socket_t, short, void *)) {
    assert(conn->pg_conn != NULL);

    // ensure we have a valid socket, this should be the case after the PQstatus check...
    if (PQsocket(conn->pg_conn) < 0)
        FATAL("PQsocket gave invalid socket");

    // reschedule with a new event
    if (conn->ev) {
        event_assign(conn->ev, conn->ev_base, PQsocket(conn->pg_conn), what, handler, conn);

    } else {
        if ((conn->ev = event_new(conn->ev_base, PQsocket(conn->pg_conn), what, handler, conn)) == NULL)
            PERROR("event_new");

    }

    // add it
    // XXX: timeouts?
    if (event_add(conn->ev, NULL))
        PERROR("event_add");
    
    // success
    return 0;

error:
    return -1;
}

/*
 * Handle events on the PQ socket while connecting
 */
static void _evpq_connect_event (evutil_socket_t fd, short what, void *arg) {
    struct evpq_conn *conn = arg;
    PostgresPollingStatusType poll_status;
    
    // this is only for connect events
    assert(conn->state == EVPQ_CONNECT);
    
    // XXX: timeouts?

    // ask PQ what to do 
    switch ((poll_status = PQconnectPoll(conn->pg_conn))) {
        case PGRES_POLLING_READING:
            // poll for read
            what = EV_READ;
            
            // reschedule
            break;

        case PGRES_POLLING_WRITING:
            // poll for write
            what = EV_WRITE;
            
            // reschedule
            break;

        case PGRES_POLLING_OK:
            // connected
            _evpq_connect_ok(conn);
            
            // done
            return;
        
        case PGRES_POLLING_FAILED:
            // faaaaail!
            _evpq_connect_fail(conn);

            // done
            return;
        
        default:
            FATAL("PQconnectPoll gave a weird value: %d", poll_status);
    }
    
    // reschedule
    if (_evpq_schedule(conn, what, _evpq_connect_event))
        goto error;

    // done, wait for the next event
    return;

error:
    // XXX: reset?
    _evpq_failure(conn);
}

static void _evpq_query_event (evutil_socket_t fd, short what, void *arg) {
    struct evpq_conn *conn = arg;
    
    // this is only for query events
    assert(conn->state == EVPQ_QUERY);

    // XXX: PQflush, timeouts
    assert(what == EV_READ);

    // we're going to assume that all queries will *require* data for their results
    // this would break otherwise (PQconsumeInput might block?)
    assert(PQisBusy(conn->pg_conn) != 0);

    // handle input
    if (PQconsumeInput(conn->pg_conn) == 0)
        ERROR("PQconsumeInput: %s", PQerrorMessage(conn->pg_conn));
    
    // handle results
    while (PQisBusy(conn->pg_conn) == 0) {
        // handle the result
        if (_evpq_query_result(conn) == 1) {
            // no need to wait for anything anymore
            return;
        }

        // loop to handle the next result
    }

    // still need to wait for a result, so reschedule
    if (_evpq_schedule(conn, EV_READ, _evpq_query_event))
        goto error;
        
    // done, wait for the next event
    return;

error:
    // XXX: reset?
    _evpq_failure(conn);

}

struct evpq_conn *evpq_connect (struct event_base *ev_base, const char *conninfo, const struct evpq_callback_info cb_info, void *cb_arg) {
    struct evpq_conn *conn = NULL;
    
    // alloc our context
    if ((conn = calloc(1, sizeof(*conn))) == NULL)
        ERROR("calloc");
    
    // initial state
    conn->ev_base = ev_base;
    conn->user_cb = cb_info;
    conn->user_cb_arg = cb_arg;
    conn->state = EVPQ_INIT;

    // create our PGconn
    if ((conn->pg_conn = PQconnectStart(conninfo)) == NULL)
        PERROR("PQconnectStart");
    
    // check for immediate failure
    if (PQstatus(conn->pg_conn) == CONNECTION_BAD)
        ERROR("PQstatus indicates CONNECTION_BAD after PQconnectStart");
    
    // assume PGRES_POLLING_WRITING
    if (_evpq_schedule(conn, EV_WRITE, _evpq_connect_event))
        goto error;

    // connecting
    conn->state = EVPQ_CONNECT;

    // success, wait for the connection to be established
    return conn;

error:
    if (conn) {
        if (conn->pg_conn)
            PQfinish(conn->pg_conn);
        
        free(conn);
    }

    return NULL;
}

int evpq_query (struct evpq_conn *conn, const char *command) {
    // check state
    if (conn->state != EVPQ_CONNECTED)
        ERROR("invalid evpq state: %d", conn->state);
    
    // do the query
    if (PQsendQuery(conn->pg_conn, command) == 0)
        ERROR("PQsendQuery: %s", PQerrorMessage(conn->pg_conn));
    
    // update state
    conn->state = EVPQ_QUERY;
    
    // XXX: PQflush

    // poll for read
    if (_evpq_schedule(conn, EV_READ, _evpq_query_event))
        goto error;

    // and then we wait
    return 0;

error:
    return -1;
}

enum evpq_state evpq_state (struct evpq_conn *conn) {
    return conn->state;
}

const PGconn *evpq_pgconn (struct evpq_conn *conn) {
    return conn->pg_conn;
}
