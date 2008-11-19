
#include <event2/event.h>

#include "evsql.h"
#include "lib/log.h"
#include "lib/signals.h"
#include "lib/misc.h"

#define CONNINFO_DEFAULT "dbname=dbfs port=5433"

void db_results (struct evsql_result *result, void *arg) {
    uint32_t val;

    static struct evsql_result_info result_info = {
        0, {
            {   EVSQL_FMT_BINARY,   EVSQL_TYPE_UINT32   },
            {   0,                  0                   }
        }
    };


}

void do_query (struct evsql *db) {
    struct evsql_query *query = NULL;

    static struct evsql_query_info query_info = {
        .sql    = "SELECT $1::int4 + 5",

        .params = {
            {   EVSQL_FMT_BINARY,   EVSQL_TYPE_UINT32   },
            {   0,                  0                   }
        }
    };

    // query
    assert((query = evsql_query_exec(db, NULL, &query_info, (uint32_t) 4, db_results, NULL)) != NULL);
}

int main (char argc, char **argv) {
    struct event_base *ev_base = NULL;
    struct signals *signals = NULL;
    struct evsql *db = NULL;

    const char *db_conninfo;
    
    // parse args
    db_conninfo = CONNINFO_DEFAULT;
    
    // init libevent
    if ((ev_base = event_base_new()) == NULL)
        ERROR("event_base_new");
    
    // setup signals
    if ((signals = signals_default(ev_base)) == NULL)
        ERROR("signals_default");

    // setup evsql
    if ((db = evsql_new_pq(ev_base, db_conninfo, NULL, NULL)) == NULL)
        ERROR("evsql_new_pq");

    // run libevent
    INFO("running libevent loop");

    if (event_base_dispatch(ev_base))
        PERROR("event_base_dispatch");
    
    // clean shutdown

error :
    if (db) {
        /* evsql_close(db); */
    }

    if (signals)
        signals_free(signals);

    if (ev_base)
        event_base_free(ev_base);
    
}

