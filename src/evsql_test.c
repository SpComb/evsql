
#include "evsql.h"
#include "lib/log.h"
#include "lib/signals.h"
#include "lib/misc.h"

#include <event2/event.h>
#include <assert.h>

#define CONNINFO_DEFAULT "dbname=dbfs port=5433"

void query_results (struct evsql_result *result, void *arg) {
    uint32_t val;

    static struct evsql_result_info result_info = {
        0, {
            {   EVSQL_FMT_BINARY,   EVSQL_TYPE_UINT32   },
            {   0,                  0                   }
        }
    };

    // begin
    assert(evsql_result_begin(&result_info, result) == 0);

    // one row
    assert(evsql_result_next(result, 
        &val
    ) > 0);

    // print
    INFO("[evsql_test.results] got result: %p: val=%lu", result, (unsigned long) val);

    // done
    evsql_result_end(result);
}

void query_send (struct evsql *db, struct evsql_trans *trans) {
    struct evsql_query *query = NULL;
    static int query_id = 0;

    static struct evsql_query_info query_info = {
        .sql    = "SELECT $1::int4 + 5",

        .params = {
            {   EVSQL_FMT_BINARY,   EVSQL_TYPE_UINT32   },
            {   0,                  0                   }
        }
    };

    // query
    assert((query = evsql_query_exec(db, trans, &query_info, query_results, NULL,
        (uint32_t) ++query_id
    )) != NULL);

    INFO("[evsql_test.query_send] enqueued query, trans=%p: %p: %d", trans, query, query_id);
}

void trans_create_result (struct evsql_result *res, void *arg) {
    // check
    if (evsql_result_check(res))
        FATAL("query failed: %s", evsql_result_error(res));
    
    INFO("[evsql_test.create_result] table created succesfully: %p", res);

    // free
    evsql_result_free(res);
}

void trans_create_query (struct evsql *db, struct evsql_trans *trans) {
    struct evsql_query *query = NULL;

    // the query info
    static struct evsql_query_info query_info = {
        .sql    = "CREATE TEMPORARY TABLE evsql_test ( id serial4, str varchar(32) DEFAULT $1::varchar ) ON COMMIT DROP",

        .params = {
            {   EVSQL_FMT_BINARY,   EVSQL_TYPE_STRING   },
            {   0,                  0,                  }
        }
    };

    // run the query
    assert((query = evsql_query_exec(db, trans, &query_info, trans_create_result, NULL,
        (const char *) "foobar"
    )) != NULL);

    INFO("[evsql_test.trans_create_query] enqueued query: %p", query);
}

void trans_error (struct evsql_trans *trans, void *arg) {
    struct evsql *db = arg;

    FATAL("[evsql_test.trans_error] failure: %s", evsql_trans_error(trans));
}

void trans_ready (struct evsql_trans *trans, void *arg) {
    struct evsql *db = arg;

    INFO("[evsql_test.trans_ready] ready");
    
    trans_create_query(db, trans);
}

void trans_done (struct evsql_trans *trans, void *arg) {
    struct evsql *db = arg;

    INFO("[evsql_test.trans_done] done");
}

void begin_transaction (struct evsql *db) {
    struct evsql_trans *trans;

    assert((trans = evsql_trans(db, EVSQL_TRANS_DEFAULT, 
        &trans_error, &trans_ready, &trans_done,
        db
    )) != NULL);

    INFO("[evsql_test.begin_trans] created transaction");
 }

int main (int argc, char **argv) {
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

    // send query
    query_send(db, NULL);

    // being transaction
    begin_transaction(db);

    // send query
    query_send(db, NULL);

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

