
#include "evsql.h"
#include "lib/log.h"
#include "lib/signals.h"
#include "lib/misc.h"

#include <event2/event.h>
#include <event2/event_struct.h>
#include <assert.h>

#define CONNINFO_DEFAULT "dbname=dbfs port=5433"

struct evsql_test_ctx {
    struct evsql *db;
    struct evsql_trans *trans;
};

// forward-declare
void query_send (struct evsql *db, struct evsql_trans *trans);


void query_timer (int fd, short what, void *arg) {
    struct evsql *db = arg;
    
    INFO("[evsql_test.timer] *tick*");

    query_send(db, NULL);
}

void query_start (struct event_base *base, struct evsql *db) {
    static struct event ev;
    struct timeval tv = { 5, 0 };

    evperiodic_assign(&ev, base, &tv, &query_timer, db);
    event_add(&ev, &tv);

    INFO("[evsql_test.timer_start] started timer");
}

void query_results (struct evsql_result *result, void *arg) {
    struct evsql *db = arg;
    uint32_t val;

    (void) db;

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
    if ((query = evsql_query_exec(db, trans, &query_info, query_results, db,
        (uint32_t) ++query_id
    )) == NULL)
        WARNING("evsql_query_exec failed");

    INFO("[evsql_test.query_send] enqueued query, trans=%p: %p: %d", trans, query, query_id);
}

void trans_commit (struct evsql_test_ctx *ctx) {
    if (evsql_trans_commit(ctx->trans))
        FATAL("evsql_trans_commit failed");
    
    INFO("[evsql_test.trans_commit] commiting transaction");
}

void trans_insert_result (struct evsql_result *res, void *arg) {
    struct evsql_test_ctx *ctx = arg;
    err_t err;
    
    // the result info
    uint32_t id;
    const char *str;

    static struct evsql_result_info result_info = {
        0, {
            {   EVSQL_FMT_BINARY,   EVSQL_TYPE_UINT32   },
            {   EVSQL_FMT_BINARY,   EVSQL_TYPE_STRING   },
            {   0,                  0                   }
        }
    };

    // begin
    if ((err = evsql_result_begin(&result_info, res)))
        EFATAL(err, "query failed%s", err == EIO ? evsql_result_error(res) : "");
    
    INFO("[evsql_test.insert] got %zu rows:", evsql_result_rows(res));

    // iterate over rows
    while ((err = evsql_result_next(res, &id, &str)) > 0) {
        INFO("\t%-4lu %s", (unsigned long) id, str);
    }

    if (err)
        EFATAL(err, "evsql_result_next failed");
    
    INFO("\t(done)");

    // done
    evsql_result_end(res);

    // commit the transaction
    trans_commit(ctx);
}

void trans_insert (struct evsql_test_ctx *ctx) {
    struct evsql_query *query = NULL;

    // the query info
    static struct evsql_query_info query_info = {
        .sql    = "INSERT INTO evsql_test (str) VALUES ($1::varchar), ($2::varchar) RETURNING id, str",

        .params = {
            {   EVSQL_FMT_BINARY,   EVSQL_TYPE_STRING   },
            {   EVSQL_FMT_BINARY,   EVSQL_TYPE_STRING   },
            {   0,                  0                   }
        }
    };

    // run the query
    assert((query = evsql_query_exec(ctx->db, ctx->trans, &query_info, trans_insert_result, ctx,
        (const char *) "row A",
        (const char *) "row B"
    )) != NULL);

    INFO("[evsql_test.insert] enqueued query: %p", query);
}

void trans_create_result (struct evsql_result *res, void *arg) {
    struct evsql_test_ctx *ctx = arg;

    // check
    if (evsql_result_check(res))
        FATAL("query failed: %s", evsql_result_error(res));
    
    INFO("[evsql_test.create_result] table created succesfully: %p", res);

    // free result
    evsql_result_free(res);

    // insert
    trans_insert(ctx);
}

void trans_create_query (struct evsql_test_ctx *ctx) {
    struct evsql_query *query = NULL;

    // the query info
    static struct evsql_query_info query_info = {
        .sql    = "CREATE TEMPORARY TABLE evsql_test ( id serial4, str varchar(32) DEFAULT 'foobar' ) ON COMMIT DROP",

        .params = {
//            {   EVSQL_FMT_BINARY,   EVSQL_TYPE_STRING   },
            {   0,                  0,                  }
        }
    };

    // run the query
    assert((query = evsql_query_exec(ctx->db, ctx->trans, &query_info, trans_create_result, ctx
//        (const char *) "foobar"
    )) != NULL);

    INFO("[evsql_test.trans_create_query] enqueued query: %p", query);
}

void trans_error (struct evsql_trans *trans, void *arg) {
    struct evsql_test_ctx *ctx = arg;

    FATAL("[evsql_test.trans_error] failure: trans=%p: %s", ctx->trans, evsql_trans_error(trans));
}

void trans_ready (struct evsql_trans *trans, void *arg) {
    struct evsql_test_ctx *ctx = arg;

    INFO("[evsql_test.trans_ready] ready");
    
    trans_create_query(ctx);
}

void trans_done (struct evsql_trans *trans, void *arg) {
    struct evsql_test_ctx *ctx = arg;

    INFO("[evsql_test.trans_done] done: trans=%p", ctx->trans);
}

void begin_transaction (struct evsql_test_ctx *ctx) {
    assert((ctx->trans = evsql_trans(ctx->db, EVSQL_TRANS_DEFAULT, 
        &trans_error, &trans_ready, &trans_done,
        ctx
    )) != NULL);

    INFO("[evsql_test.begin_trans] created transaction");
 }

int main (int argc, char **argv) {
    struct evsql_test_ctx ctx;
    struct event_base *ev_base = NULL;
    struct signals *signals = NULL;

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
    if ((ctx.db = evsql_new_pq(ev_base, db_conninfo, NULL, NULL)) == NULL)
        ERROR("evsql_new_pq");

    // send query
    query_send(ctx.db, NULL);

    // being transaction
    begin_transaction(&ctx);

    // send query
    query_send(ctx.db, NULL);

    // start query timer
    query_start(ev_base, ctx.db);

    // run libevent
    INFO("[evsql_test.main] running libevent loop");

    if (event_base_dispatch(ev_base))
        PERROR("event_base_dispatch");
    
    // clean shutdown

error :
    if (ctx.db) {
        /* evsql_close(db); */
    }

    if (signals)
        signals_free(signals);

    if (ev_base)
        event_base_free(ev_base);
    
}

