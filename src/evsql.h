#ifndef EVSQL_H
#define EVSQL_H

/*
 * An event-based (Postgre)SQL client API using libevent
 */

// XXX: remove libpq?
#include <stdint.h>
#include <postgresql/libpq-fe.h>
#include <event2/event.h>

#include <lib/err.h>

/*
 * The generic context handle
 */
struct evsql;

/*
 * Transaction handle
 */
struct evsql_trans;

/*
 * A query handle
 */
struct evsql_query;

/*
 * A result handle
 */
struct evsql_result;


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
 * Parameters and Result fields are both items
 */
enum evsql_item_format {
    EVSQL_FMT_TEXT,
    EVSQL_FMT_BINARY,
};

enum evsql_item_type {
    EVSQL_TYPE_INVALID,

    EVSQL_TYPE_NULL_,
    
    EVSQL_TYPE_BINARY,
    EVSQL_TYPE_STRING,
    EVSQL_TYPE_UINT16,
    EVSQL_TYPE_UINT32,
    EVSQL_TYPE_UINT64,
};

struct evsql_item_binary {
    const char *ptr;
    size_t len;
};

/*
 * Metadata about the type of an item
 */
struct evsql_item_info {
    // format
    enum evsql_item_format format;
    
    // type
    enum evsql_item_type type;

    // flags
    struct evsql_item_flags {
        uint8_t null_ok : 1;
    } flags;
};

/*
 * The type and value of an item
 */
struct evsql_item {
    // "header"
    struct evsql_item_info info;

    // pointer to the raw databytes. Set to NULL to indicate SQL-NULL
    const char *bytes;

    // size of byte array pointed to by bytes, zero for text
    size_t length;

    // the decoded value
    union {
        uint16_t uint16;
        uint32_t uint32;
        uint64_t uint64;
    } value;
};

/*
 * Query info, similar to prepared statements
 *
 * Contains the literal SQL query and the types of the arguments
 */
struct evsql_query_info {
    // the SQL query itself
    const char *sql;

    // the list of items
    struct evsql_item_info params[];
};

/*
 * Contains the query parameter types and their values
 */
struct evsql_query_params {
    // result format
    enum evsql_item_format result_format;
    
    // list of params
    struct evsql_item list[];
};

/*
 * Result info
 *
 * Contains the types of the result columns
 */
struct evsql_result_info {
    // XXX: put something useful here?
    int _unused;

    // the list of fields
    struct evsql_item_info columns[];
};

/*
 * Callback for handling query results.
 *
 * The query has completed, either succesfully or unsuccesfully (nonzero .error).
 *
 * Use the evsql_result_* functions to manipulate the results.
 */
typedef void (*evsql_query_cb)(struct evsql_result *res, void *arg);

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
struct evsql_query *evsql_query_params (struct evsql *evsql, struct evsql_trans *trans, 
    const char *command, const struct evsql_query_params *params, 
    evsql_query_cb query_fn, void *cb_arg
);

/*
 * Execute the given query_info, using the parameter list in query_info to resolve the given variable arugments
 */
struct evsql_query *evsql_query_exec (struct evsql *evsql, struct evsql_trans *trans, 
    const struct evsql_query_info *query_info,
    evsql_query_cb query_fn, void *cb_arg,
    ...
);

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
int evsql_param_binary (struct evsql_query_params *params, size_t param, const char *ptr, size_t len);
int evsql_param_string (struct evsql_query_params *params, size_t param, const char *ptr);
int evsql_param_uint16 (struct evsql_query_params *params, size_t param, uint16_t uval);
int evsql_param_uint32 (struct evsql_query_params *params, size_t param, uint32_t uval);
int evsql_param_null   (struct evsql_query_params *params, size_t param);
int evsql_params_clear (struct evsql_query_params *params);

/*
 * Query-handling functions
 */

// print out a textual repr of the given query/params via DEBUG
void evsql_query_debug (const char *sql, const struct evsql_query_params *params);

/*
 * Result-handling functions
 */

// get error message associated with function
const char *evsql_result_error (const struct evsql_result *res);

/*
 * Iterator-based interface.
 *
 * Call result_begin to check for errors, then result_next to fetch rows, and finally result_end to release.
 */
err_t evsql_result_begin (struct evsql_result_info *info, struct evsql_result *res);
int evsql_result_next (struct evsql_result *res, ...);
void evsql_result_end (struct evsql_result *res);

// number of rows in the result
size_t evsql_result_rows (const struct evsql_result *res);

// number of columns in the result
size_t evsql_result_cols (const struct evsql_result *res);

// number of affected rows for UPDATE/INSERT
size_t evsql_result_affected (const struct evsql_result *res);

// fetch the raw binary value from a result set, and return it via ptr
// if size is nonzero, check that the size of the field data matches
int evsql_result_binary (const struct evsql_result *res, size_t row, size_t col, const char **ptr, size_t *size, int nullok);
int evsql_result_string (const struct evsql_result *res, size_t row, size_t col, const char **ptr, int nullok);

// fetch certain kinds of values from a binary result set
int evsql_result_uint16 (const struct evsql_result *res, size_t row, size_t col, uint16_t *uval, int nullok);
int evsql_result_uint32 (const struct evsql_result *res, size_t row, size_t col, uint32_t *uval, int nullok);
int evsql_result_uint64 (const struct evsql_result *res, size_t row, size_t col, uint64_t *uval, int nullok);

// release the result set, freeing its memory
void evsql_result_free (struct evsql_result *res);

/*
 * Close a connection. Callbacks for waiting queries will not be run.
 *
 * XXX: not implemented yet.
 */
void evsql_close (struct evsql *evsql);

#endif /* EVSQL_H */
