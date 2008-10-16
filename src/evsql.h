#ifndef EVSQL_H
#define EVSQL_H

/*
 * An event-based (Postgre)SQL client API using libevent
 */

// XXX: libpq
#include <stdint.h>
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
 * Transaction handle
 */
struct evsql_trans;

/*
 * Transaction type
 */
enum evsql_trans_type {
    EVSQL_TRANS_DEFAULT,
    EVSQL_TRANS_SERIALIZABLE,
    EVSQL_TRANS_REPEATABLE_READ,
    EVSQL_TRANS_READ_COMMITTED,
    EVSQL_TRANS_READ_UNCOMMITTED,
};

/*
 * Parameter type
 */
enum evsql_param_format {
    EVSQL_FMT_TEXT,
    EVSQL_FMT_BINARY,
};

enum evsql_param_type {
    EVSQL_PARAM_INVALID,

    EVSQL_PARAM_NULL,

    EVSQL_PARAM_STRING,
    EVSQL_PARAM_UINT16,
    EVSQL_PARAM_UINT32,
    EVSQL_PARAM_UINT64,
};

/*
 * Query parameter info.
 *
 * Use the EVSQL_PARAM_* macros to define the value of list
 */
struct evsql_query_params {
    // nonzero to get results in binary format
    enum evsql_param_format result_fmt;
    
    // the list of parameters, terminated by { 0, 0 }
    struct evsql_query_param {
        // the param type
        enum evsql_param_type type;
        
        // pointer to the raw data
        const char *data_raw;
        
        // the value
        union {
            uint16_t uint16;
            uint32_t uint32;
            uint64_t uint64;
        } data;

        // the explicit length of the parameter if it's binary, zero for text.
        // set to -1 to indicate that the value is still missing
        ssize_t length;
    } list[];
};

// macros for defining evsql_query_params
#define EVSQL_PARAMS(result_fmt)            { result_fmt, 
#define EVSQL_PARAM(typenam)                    { EVSQL_PARAM_ ## typenam, NULL }
#define EVSQL_PARAMS_END                        { EVSQL_PARAM_INVALID, NULL } \
                                              } // <<<

/*
 * Result type
 */
struct evsql_result_info {
    struct evsql *evsql;
    struct evsql_trans *trans;
    
    int error;

    union evsql_result {
        // libpq
        PGresult *pq;
    } result;
};

/*
 * Callback for handling query results.
 *
 * The query has completed, either succesfully or unsuccesfully (nonzero .error).
 *
 * Use the evsql_result_* functions to manipulate the results.
 */
typedef void (*evsql_query_cb)(const struct evsql_result_info *res, void *arg);

/*
 * Callback for handling global-level errors.
 *
 * The evsql is not useable anymore.
 *
 * XXX: this is not actually called yet, no retry logic implemented.
 */
typedef void (*evsql_error_cb)(struct evsql *evsql, void *arg);

/*
 * Callback for handling transaction-level errors.
 *
 * The transaction is not useable anymore.
 */
typedef void (*evsql_trans_error_cb)(struct evsql_trans *trans, void *arg);

/*
 * The transaction is ready for use.
 */
typedef void (*evsql_trans_ready_cb)(struct evsql_trans *trans, void *arg);

/*
 * The transaction was commited, and should not be used anymore.
 */
typedef void (*evsql_trans_done_cb)(struct evsql_trans *trans, void *arg);

/*
 * Create a new PostgreSQL/libpq(evpq) -based evsql using the given conninfo.
 *
 * The given conninfo must stay valid for the duration of the evsql's lifetime.
 */
struct evsql *evsql_new_pq (struct event_base *ev_base, const char *pq_conninfo, evsql_error_cb error_fn, void *cb_arg);

/*
 * Create a new transaction.
 *
 * Transactions are separate connections that provide transaction-isolation.
 *
 * Once the transaction is ready for use, ready_fn will be called. If the transaction fails, any pending query will be
 * forgotten, and error_fn called. This also includes some (but not all) cases where evsql_query returns nonzero.
 *
 */
struct evsql_trans *evsql_trans (struct evsql *evsql, enum evsql_trans_type type, evsql_trans_error_cb error_fn, evsql_trans_ready_cb ready_fn, evsql_trans_done_cb done_fn, void *cb_arg);

/*
 * Queue the given query for execution.
 *
 * If trans is specified (not NULL), then the transaction must be idle, and the query will be executed in that
 * transaction's context. Otherwise, the query will be executed without a transaction, andmay be executed immediately,
 * or if other similar queries are running, it will be queued for later execution.
 *
 * Once the query is complete (got a result, got an error, the connection failed), then the query_cb will be triggered.
 */
struct evsql_query *evsql_query (struct evsql *evsql, struct evsql_trans *trans, const char *command, evsql_query_cb query_fn, void *cb_arg);

/*
 * Same as evsql_query, but uses the SQL-level support for binding parameters.
 */
struct evsql_query *evsql_query_params (struct evsql *evsql, struct evsql_trans *trans, const char *command, const struct evsql_query_params *params, evsql_query_cb query_fn, void *cb_arg);

/*
 * Abort a query, the query callback will not be called, the query and any possible results will be discarded.
 *
 * This does not garuntee that the query will not execute, simply that you won't get the results.
 *
 * If the query is part of a transaction, then trans must be given, and the query must be the query that is currently
 * executing on that trans. The transaction's ready_fn will be called once the query has been aborted.
 */
void evsql_query_abort (struct evsql_trans *trans, struct evsql_query *query);

/*
 * Commit a transaction, calling done_fn if it was succesfull (error_fn otherwise).
 *
 * trans must be idle, just like for evsql_query.
 *
 * done_fn will never be called directly, always via the event loop.
 *
 * You cannot abort a COMMIT, calling trans_abort on trans after a succesful trans_commit is a FATAL error.
 */
int evsql_trans_commit (struct evsql_trans *trans);

/*
 * Abort a transaction, rolling it back. No callbacks will be called.
 *
 * You cannot abort a COMMIT, calling trans_abort on trans after a succesful trans_commit is a FATAL error.
 */
void evsql_trans_abort (struct evsql_trans *trans);

/*
 * Transaction-handling functions
 */

// error string, meant to be called from evsql_trans_error_cb
const char *evsql_trans_error (struct evsql_trans *trans);

/*
 * Param-building functions
 */
int evsql_param_string (struct evsql_query_params *params, size_t param, const char *ptr);
int evsql_param_uint32 (struct evsql_query_params *params, size_t param, uint32_t uval);

/*
 * Result-handling functions
 */

// get error message associated with function
const char *evsql_result_error (const struct evsql_result_info *res);

// number of rows in the result
size_t evsql_result_rows (const struct evsql_result_info *res);

// number of columns in the result
size_t evsql_result_cols (const struct evsql_result_info *res);

// fetch the raw binary value from a result set, and return it via ptr
// if size is nonzero, check that the size of the field data matches
int evsql_result_buf    (const struct evsql_result_info *res, size_t row, size_t col, const char **ptr, size_t *size, int nullok);
int evsql_result_binary (const struct evsql_result_info *res, size_t row, size_t col, const char **ptr, size_t size, int nullok);
int evsql_result_string (const struct evsql_result_info *res, size_t row, size_t col, const char **ptr, int nullok);

// fetch certain kinds of values from a binary result set
int evsql_result_uint16 (const struct evsql_result_info *res, size_t row, size_t col, uint16_t *uval, int nullok);
int evsql_result_uint32 (const struct evsql_result_info *res, size_t row, size_t col, uint32_t *uval, int nullok);
int evsql_result_uint64 (const struct evsql_result_info *res, size_t row, size_t col, uint64_t *uval, int nullok);

// release the result set, freeing its memory
void evsql_result_free (const struct evsql_result_info *res);

// platform-dependant aliases
#define evsql_result_ushort evsql_result_uint16

#if _LP64
#define evsql_result_ulong evsql_result_uint64
#else
#define evsql_result_ulong evsql_result_uint32
#endif /* _LP64 */

/*
 * Close a connection. Callbacks for waiting queries will not be run.
 *
 * XXX: not implemented yet.
 */
void evsql_close (struct evsql *evsql);

#endif /* EVSQL_H */
