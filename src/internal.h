#ifndef EVSQL_INTERNAL_H
#define EVSQL_INTERNAL_H

/*
 * Internal interfaces
 */

#include <sys/queue.h>

#include <event2/event.h>

#include "evsql.h"
#include "evpq.h"

/*
 * The engine type
 */
enum evsql_type {
    EVSQL_EVPQ,     // evpq
};

/*
 * Contains the type, engine configuration, list of connections and waiting query queue.
 */
struct evsql {
    // what event_base to use
    struct event_base *ev_base;

    // what engine we use
    enum evsql_type type;

    // callbacks
    evsql_error_cb error_fn;
    void *cb_arg;
    
    // engine-specific connection configuration
    union {
        const char *evpq;
    } engine_conf;

    // list of connections that are open
    LIST_HEAD(evsql_conn_list, evsql_conn) conn_list;
   
    // list of queries running or waiting to run
    TAILQ_HEAD(evsql_query_queue, evsql_query) query_queue;
};

/*
 * A single connection to the server.
 *
 * Contains the engine connection, may have a transaction associated, and may have a query associated.
 */
struct evsql_conn {
    // evsql we belong to
    struct evsql *evsql;

    // engine-specific connection info
    union {
        struct evpq_conn *evpq;
    } engine;

    // our position in the conn list
    LIST_ENTRY(evsql_conn) entry;

    // are we running a transaction?
    struct evsql_trans *trans;

    // are we running a transactionless query?
    struct evsql_query *query;
};

/*
 * A single transaction.
 *
 * Has a connection associated and possibly a query (which will also be associated with the connection)
 */
struct evsql_trans {
    // our evsql_conn/evsql
    struct evsql *evsql;
    struct evsql_conn *conn;
    
    // callbacks
    evsql_trans_error_cb error_fn;
    evsql_trans_ready_cb ready_fn;
    evsql_trans_done_cb done_fn;
    void *cb_arg;

    // the transaction type
    enum evsql_trans_type type;

    // has evsql_trans_commit be called?
    int has_commit : 1;

    // our current query
    struct evsql_query *query;

};

/*
 * Backend result handle
 */
union evsql_result_handle {
    PGresult *pq;
};

/*
 * A single query.
 *
 * Has the info needed to exec the query (as these may be queued), and the callback/result info.
 */
struct evsql_query {
    // the actual SQL query, this may or may not be ours, see _evsql_query_exec
    char *command;
    
    // possible query params
    struct evsql_query_params_pq {
        int count;

        Oid *types;
        const char **values;
        int *lengths;
        int *formats;

        // storage for numeric values
        union evsql_item_value *item_vals;

        int result_format;
    } params;

    // our callback
    evsql_query_cb cb_fn;
    void *cb_arg;
        
    // the result we get
    union evsql_result_handle result;

    // our position in the query list
    TAILQ_ENTRY(evsql_query) entry;
};

// the result
struct evsql_result {
    struct evsql *evsql;

    // possible error code
    int error;
    
    // the actual result
    union evsql_result_handle result;

    // result_* state
    struct evsql_result_info *info;
    size_t row_offset;
};


// maximum length for a 'BEGIN TRANSACTION ...' query
#define EVSQL_QUERY_BEGIN_BUF 512

// the should the OID of some valid psql type... *ANY* valid psql type, doesn't matter, only used for NULLs
// 16 = bool in 8.3
#define EVSQL_PQ_ARBITRARY_TYPE_OID 16

/*
 * Core query-submission interface.
 *
 * This performs some error-checking on the trans, allocates the evsql_query and does some basic initialization.
 *
 * This does not actually enqueue the query anywhere, no reference is stored anywhere.
 *
 * Returns the new evsql_query on success, NULL on failure.
 */
struct evsql_query *_evsql_query_new (struct evsql *evsql, struct evsql_trans *trans, evsql_query_cb query_fn, void *cb_arg);

/*
 * Begin processing the given query, which should now be fully filled out.
 *
 * If trans is given, it MUST be idle, and the query will be executed. Otherwise, it will either be executed directly
 * or enqueued for future execution.
 *
 * Returns zero on success, nonzero on failure.
 */
int _evsql_query_enqueue (struct evsql *evsql, struct evsql_trans *trans, struct evsql_query *query, const char *command);

/*
 * Free the query and related resources, doesn't trigger any callbacks or remove from any queues.
 *
 * The command should already be taken care of (NULL).
 */
void _evsql_query_free (struct evsql_query *query);

#endif /* EVSQL_INTERNAL_H */
