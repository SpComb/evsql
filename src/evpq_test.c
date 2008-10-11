
#include <stdio.h>

#include "evpq.h"
#include "lib/log.h"

#define CONNINFO_DEFAULT "dbname=test"
#define QUERY_DEFAULT "SELECT a, b FROM foo"

void cb_connected (struct evpq_conn *conn, void *arg) {
    INFO("[evpq_test] connected");

    if (evpq_query(conn, QUERY_DEFAULT))
        FATAL("evpq_query");
}

void cb_result (struct evpq_conn *conn, PGresult *result, void *arg) {

    INFO("[evpq_test] result: %s", PQresStatus(PQresultStatus(result)));

    // fatal error?
    if (PQresultStatus(result) != PGRES_TUPLES_OK)
        FATAL("error: %s", PQresultErrorMessage(result));
    
    // dump it to stdout
    PQprintOpt popt = {
        .header     = 1,
        .align      = 1,
        .standard   = 0,
        .html3      = 0,
        .expanded   = 1,
        .pager      = 0,
        .fieldSep   = "|",
        .tableOpt   = NULL,
        .caption    = NULL,
        .fieldName  = NULL,
    };

    PQprint(stdout, result, &popt);

    // don't care about the result anymore
    PQclear(result);
}

void cb_done (struct evpq_conn *conn, void *arg) {
    INFO("[evpq_test] done");
}

void cb_failure (struct evpq_conn *conn, void *arg) {
    INFO("[evpq_test] failure");
    INFO("\t%s", evpq_error_message(conn));
    
    FATAL("exiting");
}

int main (int argc, char **argv) {
    struct event_base *ev_base = NULL;
    struct evpq_conn *conn = NULL;
    const char *conninfo = CONNINFO_DEFAULT;

    struct evpq_callback_info cb_info = {
        .fn_connected = cb_connected,
        .fn_result = cb_result,
        .fn_done = cb_done,
        .fn_failure = cb_failure,
    };

    // initialize libevent
    if ((ev_base = event_base_new()) == NULL)
        ERROR("event_base_new");

    // establish the evpq connection
    if ((conn = evpq_connect(ev_base, conninfo, cb_info, NULL)) == NULL)
        ERROR("evpq_connect");

    // run libevent
    INFO("running libevent loop");

    if (event_base_dispatch(ev_base))
        ERROR("event_base_dispatch");
    
    // clean shutdown

error:
    if (ev_base)
        event_base_free(ev_base);
}


