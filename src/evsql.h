#ifndef EVSQL_H
#define EVSQL_H

/*
 * An event-based (Postgre)SQL client API using libevent
 */

// XXX: libpq
#include <postgresql/libpq-fe.h>
#include <event2/event.h>

/*
 * The generic context handle
 */
struct evsql;

/*
 * A query handle
 */
struct evsql_query;

/*
 * Query parameter info.
 *
 * Use the EVSQL_PARAM_* macros to define the value of list
 */
struct evsql_query_params {
    // nonzero to get results in binary format
    int result_binary;
    
    // the list of parameters, terminated by { 0, 0 }
    struct evsql_query_param {
        // the textual or binary value for this parameter
        char *value;

        // the explicit length of the parameter if it's binary. Must be non-zero for NULL values.
        int length;
    } list[];
};

// macros for defining evsql_query_params
#define EVSQL_PARAM_NULL                    { NULL, 1 }
#define EVSQL_PARAM_TEXT(value)             { value, 0 }
#define EVSQL_PARAM_BINARY(value, length)   { value, length }

/*
 * Result type
 */
struct evsql_result_info {
    struct evsql *evsql;
    
    int error;

    union {
        // XXX: libpq
        PGresult *pq;

    } result;
};

/*
 * Callback for handling query-level errors.
 *
 * The query has completed, either succesfully or unsuccesfully (look at info.error).
 * info.result contains the result à la the evsql's type.
 */
typedef void (*evsql_query_cb)(struct evsql_result_info info, void *arg);

/*
 * Callback for handling connection-level errors.
 *
 * The SQL context/connection suffered an error. It is not valid anymore, and may not be used.
 */
typedef void (*evsql_error_cb)(struct evsql *evsql, void *arg);

/*
 * Create a new PostgreSQL/libpq(evpq) -based evsql using the given conninfo.
 */
struct evsql *evsql_new_pq (struct event_base *ev_base, const char *pq_conninfo, evsql_error_cb error_fn, void *cb_arg);

/*
 * Queue the given query for execution.
 */
struct evsql_query *evsql_query (struct evsql *evsql, const char *command, evsql_query_cb query_fn, void *cb_arg);

/*
 * Same, but uses the SQL-level support for binding parameters.
 */
struct evsql_query *evsql_query_params (struct evsql *evsql, const char *command, struct evsql_query_params params, evsql_query_cb query_fn, void *cb_arg);

/*
 * Close a connection. Callbacks for waiting queries will not be run.
 *
 * XXX: not implemented yet.
 */
void evsql_close (struct evsql *evsql);

#endif /* EVSQL_H */
