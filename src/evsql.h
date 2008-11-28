#ifndef EVSQL_H
#define EVSQL_H

/*
 * An event-based (Postgre)SQL client API using libevent
 */

#include <stdint.h>
#include <event2/event.h>

#include "lib/err.h"

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
 * Storage for various scalar values
 */
union evsql_item_value {
    uint16_t uint16;
    uint32_t uint32;
    uint64_t uint64;
};

/*
 * The type and value of an item
 */
struct evsql_item {
    // "header"
    struct evsql_item_info info;

    // pointer to the raw databytes. Set to NULL to indicate SQL-NULL, &value, or an external buf
    const char *bytes;

    // size of byte array pointed to by bytes, zero for text
    size_t length;

    // the decoded value
    union evsql_item_value value;
    
    // (internal) flags
    struct {
        uint8_t has_value : 1;
    } flags;
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
 * Macros for defining param/result infos/lists
 */
#define EVSQL_PARAMS(result_fmt)            { result_fmt, 
#define EVSQL_PARAM(typenam)                    { EVSQL_TYPE(typenam) }
#define EVSQL_PARAMS_END                        { EVSQL_TYPE_END } \
                                              } // <<<

#define EVSQL_TYPE(typenam)                     { EVSQL_FMT_BINARY, EVSQL_TYPE_ ## typenam  }
#define EVSQL_TYPE_END                          { EVSQL_FMT_BINARY, EVSQL_TYPE_INVALID      }


/*
 * Callback for handling query results.
 *
 * The query has completed, either succesfully or unsuccesfully.
 *
 * Use the evsql_result_* functions to manipulate the results, and call evsql_result_free (or equivalent) once done.
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
 * Callback for handling transaction-level errors. This may be called at any time during a transaction's lifetime,
 * including from within the evsql_query_* functions.
 *
 * The transaction is not useable anymore.
 */
typedef void (*evsql_trans_error_cb)(struct evsql_trans *trans, void *arg);

/*
 * Callback for handling evsql_trans/evsql_query_abort completion. The transaction is ready for use with evsql_query_*.
 */
typedef void (*evsql_trans_ready_cb)(struct evsql_trans *trans, void *arg);

/*
 * Callback for handling evsql_trans_commit completion. The transaction was commited, and should not be used anymore.
 */
typedef void (*evsql_trans_done_cb)(struct evsql_trans *trans, void *arg);

/*
 * Create a new PostgreSQL/libpq(evpq) -based evsql using the given conninfo.
 *
 * The given conninfo must stay valid for the duration of the evsql's lifetime.
 */
struct evsql *evsql_new_pq (struct event_base *ev_base, const char *pq_conninfo, 
    evsql_error_cb error_fn, 
    void *cb_arg
);

/*
 * Create a new transaction.
 *
 * A transaction will be allocated its own connection, and the "BEGIN TRANSACTION ..." query will be sent (use the
 * evsql_trans_type argument to specify this). 
 *
 * Once the transaction has been opened, the given evsql_trans_ready_cb will be triggered, and the transaction can then
 * be used (see evsql_query_*).
 *
 * If, at any point, the transaction-connection fails, and pending query will be forgotten (i.e. the query callback
 * will NOT be called), and the given evsql_trans_error_cb will be called. Note that this includes some, but not all,
 * cases where evsql_query_* returns an error.
 *
 * Once you are done with the transaction, call either evsql_trans_commit or evsql_trans_abort.
 */
struct evsql_trans *evsql_trans (struct evsql *evsql, enum evsql_trans_type type, 
    evsql_trans_error_cb error_fn, 
    evsql_trans_ready_cb ready_fn, 
    evsql_trans_done_cb done_fn, 
    void *cb_arg
);

/*
 * Queue the given query for execution.
 *
 * If trans is specified (not NULL), then the transaction must be idle, and the query will be executed in that
 * transaction's context. Otherwise, the query will be executed without a transaction using an idle connection, or
 * enqueued for later execution.
 *
 * Once the query is complete (got a result, got an error, the connection failed), then the query_cb will be called.
 * The callback can used the evsql_result_* functions to manipulate it.
 *
 * The returned evsql_query handle can be passed to evsql_query_abort at any point before query_fn being called. 
 *
 */
struct evsql_query *evsql_query (struct evsql *evsql, struct evsql_trans *trans, const char *command, evsql_query_cb query_fn, void *cb_arg);

/*
 * Execute the given SQL query using the list of parameter types/values given via evsql_query_params.
 *
 * See evsql_query for more info about behaviour.
 */
struct evsql_query *evsql_query_params (struct evsql *evsql, struct evsql_trans *trans, 
    const char *command, const struct evsql_query_params *params, 
    evsql_query_cb query_fn, void *cb_arg
);

/*
 * Execute the given query_info's SQL query using the values given as variable arguments, using the evsql_query_info to
 * resolve the types.
 *
 * See evsql_query for more info about behaviour.
 */
struct evsql_query *evsql_query_exec (struct evsql *evsql, struct evsql_trans *trans, 
    const struct evsql_query_info *query_info,
    evsql_query_cb query_fn, void *cb_arg,
    ...
);

/*
 * Abort a query returned by evsql_query_* that has not yet completed (query_fn has not been called yet).
 *
 * The actual query itself may or may not be aborted (and hence may or may not be executed on the server), but query_fn
 * will not be called anymore, and the query will dispose of itself and any results returned.
 *
 * If the query is part of a transaction, then trans must be given, and the query must be the query that is currently
 * executing on that trans. The transaction's ready_fn will be called once the query has been aborted and the
 * transaction is now idle again.
 */
void evsql_query_abort (struct evsql_trans *trans, struct evsql_query *query);

/*
 * Commit a transaction using "COMMIT TRANSACTION".
 *
 * The transaction must be idle, just like for evsql_query. Once the transaction has been commited, the transaction's
 * done_fn will be called, after which the transaction must not be used.
 *
 * You cannot abort a COMMIT, calling trans_abort on trans after a succesful trans_commit is a FATAL error.
 *
 * Note that done_fn will never be called directly, always indirectly via the event loop.
 */
int evsql_trans_commit (struct evsql_trans *trans);

/*
 * Abort a transaction, using "ROLLBACK TRANSACTION".
 *
 * No more transaction callbacks will be called, if there was a query running, it will be aborted, and the transaction
 * then rollback'd.
 *
 * You cannot abort a COMMIT, calling trans_abort on trans after a succesful trans_commit is a FATAL error.
 * 
 * Do not call evsql_trans_abort from within evsql_trans_error_cb!
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

/*
 * Check the result for errors. Intended for use with non-data queries, i.e. CREATE, etc.
 *
 * Returns zero if the query was OK, err otherwise. EIO indicates an SQL error, the error message can be retrived
 * using evsql_result_error.
 */
err_t evsql_result_check (struct evsql_result *res);

/*
 * The iterator-based interface results interface.
 *
 * Define an evsql_result_info struct that describes the columns returned by the query, and call evsql_result_begin on
 * the evsql_result. This verifies the query result, and then prepares it for iteration using evsql_result_next.
 *
 * Call evsql_result_end once you've stopped iteration.
 *
 * Returns zero if the evsql_result is ready for iteration, err otherwise. EIO indicates an SQL error, the error
 * message can be retreived using evsql_result_error.
 *
 * Note: currently the iterator state is simply stored in evsql_result, so only one iterator at a time per evsql_result.
 */
err_t evsql_result_begin (struct evsql_result_info *info, struct evsql_result *res);

/*
 * Reads the next result row, storing the field values into the pointer arguments given. The types are resolved using
 * the evsql_result_info given to evsql_result_begin.
 *
 * Returns >0 when a row was read, 0 when there are no more rows, and -err if there was an error.
 */
int evsql_result_next (struct evsql_result *res, ...);

/*
 * Ends the result iteration, releasing any associated resources and the result itself.
 *
 * The result should not be iterated or accessed anymore.
 *
 * Note: this does the same thing as evsql_result_free, and works regardless of evsql_result_begin returning
 * succesfully or not.
 */
void evsql_result_end (struct evsql_result *res);


// get error message associated with function
const char *evsql_result_error (const struct evsql_result *res);

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
