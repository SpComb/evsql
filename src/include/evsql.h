#ifndef EVSQL_H
#define EVSQL_H

/**
 * @file src/evsql.h
 *
 * A SQL library designed for use with libevent and PostgreSQL's libpq. Provides support for queueing non-transactional
 * requests, transaction support, parametrized queries and result iteration.
 *
 * Currently, the API does not expose the underlying libpq data structures, but since it is currently the only
 * underlying implementation, there is no guarantee that the same API will actually work with other databases' interface
 * libraries...
 *
 * The order of function calls and callbacks goes something like this:
 *
 *  -   evsql_new_pq()
 *
 *  -   evsql_trans()
 *      -   evsql_trans_abort()
 *      -   evsql_trans_error_cb()
 *      -   evsql_trans_ready_cb()
 *
 *  -   evsql_query(), \ref evsql_param_ + evsql_query_params(), evsql_query_exec()
 *      -   evsql_query_abort()
 *      -   evsql_query_cb()
 *          -   \ref evsql_result_
 *          -   evsql_result_free()
 *
 *  -   evsql_trans_commit()
 *      -   evsql_trans_done_cb()
 *
 */

/**
 * System includes
 */
#include <stdint.h>
#include <stdbool.h>
#include <event2/event.h>

/**
 * Type for error return codes
 */
typedef unsigned int evsql_err_t;

/**
 * @struct evsql
 *
 * The generic session handle used to manage a single "database connector" with multiple queries/transactions.
 *
 * @see \ref evsql_
 */
struct evsql;

/**
 * @struct evsql_trans
 *
 * Opaque transaction handle returned by evsql_trans() and used for the \ref evsql_query_ functions
 *
 * @see \ref evsql_trans_
 */
struct evsql_trans;

/**
 * @struct evsql_query
 *
 * Opaque query handle returned by the \ref evsql_query_ functions and used for evsql_query_abort()
 *
 * @see \ref evsql_query_
 */
struct evsql_query;

/**
 * @struct evsql_result
 *
 * Opaque result handle received by evsql_query_cb(), and used with the \ref evsql_result_ functions
 *
 * @see evsql_query_cb
 * @see \ref evsql_result_
 */
struct evsql_result;

/**
 * Various transaction isolation levels for conveniance
 *
 * @see evsql_trans
 */
enum evsql_trans_type {
    EVSQL_TRANS_DEFAULT,
    EVSQL_TRANS_SERIALIZABLE,
    EVSQL_TRANS_REPEATABLE_READ,
    EVSQL_TRANS_READ_COMMITTED,
    EVSQL_TRANS_READ_UNCOMMITTED,
};

/**
 * An item can be in different formats, the classical text-based format (i.e. snprintf "1234") or a more low-level
 * binary format (i.e uint16_t 0x04F9 in network-byte order).
 */
enum evsql_item_format {
    /** Format values as text strings */
    EVSQL_FMT_TEXT,

    /** Type-specific binary encoding */
    EVSQL_FMT_BINARY,
};

/**
 * An item has a specific type, these correspond somewhat to the native database types.
 */
enum evsql_item_type {
    /** End marker, zero */
    EVSQL_TYPE_INVALID,
    
    /** A SQL NULL */
    EVSQL_TYPE_NULL_,
    
    /** A `struct evsql_item_binary` */
    EVSQL_TYPE_BINARY,

    /** A NUL-terminated char* */
    EVSQL_TYPE_STRING,

    /** A uint16_t value */
    EVSQL_TYPE_UINT16,

    /** A uint32_t value */
    EVSQL_TYPE_UINT32,

    /** A uint64_t value */
    EVSQL_TYPE_UINT64,

    EVSQL_TYPE_MAX
};

/**
 * Value for use with EVSQL_TYPE_BINARY, this just a non-NUL-terminated char* and an explicit length
 */
struct evsql_item_binary {
    /** The binary data */
    const char *ptr;

    /** Number of bytes pointed to by ptr */
    size_t len;
};

/**
 * Metadata about the format and type of an item, this does not hold any actual value.
 */
struct evsql_item_info {
    /** The format */
    enum evsql_item_format format;
    
    /** The type type */
    enum evsql_item_type type;

    /** Various flags */
    struct evsql_item_flags {
        /** The value may be NULL @see evsql_result_next */
        bool null_ok;
    } flags;
};

/**
 * An union to provide storage for the values of small types
 *
 * @see evsql_item
 */
union evsql_item_value {
    /** 16-bit unsigned integer */
    uint16_t uint16;

    /** 32-bit unsigned integer */
    uint32_t uint32;

    /** 64-bit unsigned integer */
    uint64_t uint64;
};

/**
 * A generic structure containing the type and value of a query parameter or a result field.
 *
 * @see evsql_query_info
 * @see evsql_query_params
 * @see evsql_result_info
 */
struct evsql_item {
    /** The "header" containing the type and format */
    struct evsql_item_info info;

    /**
     * Pointer to the raw databytes. 
     * Set to NULL for SQL NULLs, otherwise &value or an external buf 
     */
    const char *bytes;

    /**
     * Size of the byte array pointed to by bytes, zero for EVSQL_FMT_TEXT data.
     */
    size_t length;

    /**
     * Inline storage for small values
     */
    union evsql_item_value value;
    
    /** Internal flags */
    struct {
        /**
         * The item has a value stored in `value`
         */
        bool has_value;
    } flags;
};

/**
 * Query meta-info, similar to a prepared statement.
 *
 * Contains the literal SQL query and the types of the parameters, but no more.
 *
 * @see evsql_query_exec
 */
struct evsql_query_info {
    /** The SQL query itself */
    const char *sql;

    /** 
     * A variable-length array of the item_info parameters, terminated by an EVSQL_TYPE_INVALID entry.
     */
    struct evsql_item_info params[];
};

/**
 * Contains the query parameter types and their actual values
 *
 * @see evsql_query_params
 */
struct evsql_query_params {
    /** Requested result format for this query. XXX: move elsewhere */
    enum evsql_item_format result_format;
    
    /**
     * A variable-length array of the item parameter-values, terminated by an EVSQL_TYPE_INVALID entry.
     */
    struct evsql_item list[];
};

/**
 * Result layout metadata. This contains the stucture needed to decode result rows.
 *
 * @see evsql_result_begin
 */
struct evsql_result_info {
    /** XXX: make up something useful to stick here */
    int _unused;

    /**
     * A variable-length array of the item_info column types.
     */
    struct evsql_item_info columns[];
};

/**
 * Magic macros for defining param/result info -lists
 *  
 * @code
 *  static struct evsql_query_params params = EVSQL_PARAMS(EVSQL_FMT_BINARY) {
 *      EVSQL_PARAM( UINT32 ),
 *      ...,
 *
 *      EVSQL_PARAMS_END
 *  };
 * @endcode
 *
 * @name EVSQL_TYPE/PARAM_*
 * @{
 */

/**
 * A `struct evsql_item_info` initializer, using FMT_BINARY and the given EVSQL_TYPE_ -suffix.
 *
 * @param typenam the suffix of an evsql_item_type name
 *
 * @see struct evsql_item_info
 * @see enum evsql_item_type
 */
#define EVSQL_TYPE(typenam)                     { EVSQL_FMT_BINARY, EVSQL_TYPE_ ## typenam, { false } }
#define EVSQL_TYPE_NULL(typenam)                { EVSQL_FMT_BINARY, EVSQL_TYPE_ ## typenam, { true } }

/**
 * End marker for a `struct evsql_item_info` array.
 *
 * @see struct evsql_item_info
 */
#define EVSQL_TYPE_END                          EVSQL_TYPE(INVALID)

/**
 * Initializer block for an evsql_query_params struct
 */
#define EVSQL_PARAMS(result_fmt)            { result_fmt, 

/**
 * An evsql_item initializer
 */
#define EVSQL_PARAM(typenam)                    { EVSQL_TYPE(typenam) }

/**
 * Include the ending item and terminate the pseudo-block started using #EVSQL_PARAMS
 */
#define EVSQL_PARAMS_END                        { EVSQL_TYPE_END } \
                                              } // <<<

// @}

/**
 * Callback definitions
 *
 * @name evsql_*_cb
 * @{
 */

/**
 * Callback for handling query results.
 *
 * The query has completed, either succesfully or unsuccesfully.
 *
 * Use the \ref evsql_result_ functions to manipulate the results, and call evsql_result_free() (or equivalent) once done.
 *
 * @param res The result handle that must be result_free'd after use
 * @param arg The void* passed to \ref evsql_query_
 *
 * @see evsql_query
 */
typedef void (*evsql_query_cb)(struct evsql_result *res, void *arg);

/**
 * Callback for handling global-level errors.
 *
 * The evsql is not useable anymore.
 *
 * XXX: this is not actually called yet, as no retry logic is implemented, so an evsql itself never fails.
 *
 * @see evsql_new_pq
 */
typedef void (*evsql_error_cb)(struct evsql *evsql, void *arg);

/**
 * Callback for handling transaction-level errors. This may be called at any time during a transaction's lifetime,
 * including from within the \ref evsql_query_ functions (but not always).
 *
 * The transaction is not useable anymore.
 *
 * @param trans the transaction in question
 * @param arg the void* passed to evsql_trans
 *
 * @see evsql_trans
 */
typedef void (*evsql_trans_error_cb)(struct evsql_trans *trans, void *arg);

/**
 * Callback for handling evsql_trans/evsql_query_abort completion. The transaction is ready for use with \ref evsql_query_.
 *
 * @param trans the transaction in question
 * @param arg the void* passed to evsql_trans
 *
 * @see evsql_trans
 * @see evsql_query_abort
 */
typedef void (*evsql_trans_ready_cb)(struct evsql_trans *trans, void *arg);

/**
 * Callback for handling evsql_trans_commit completion. The transaction was commited, and should not be used anymore.
 *
 * @param trans the transaction in question
 * @param arg the void* passed to evsql_trans
 *
 * @see evsql_trans
 * @see evsql_trans_commit
 */
typedef void (*evsql_trans_done_cb)(struct evsql_trans *trans, void *arg);

// @}

/**
 * Session functions
 *
 * @defgroup evsql_* Session interface
 * @see evsql.h
 * @{
 */

/**
 * Session creation functions
 *
 * @defgroup evsql_new_* Session creation interface
 * @see evsql.h
 * @{
 */

/**
 * Create a new PostgreSQL/libpq (evpq) -based evsql using the given conninfo.
 *
 * The given \a pq_conninfo pointer must stay valid for the duration of the evsql's lifetime.
 *
 * See the libpq reference manual for the syntax of pq_conninfo
 *
 * @param ev_base the libevent base to use
 * @param pq_conninfo the libpq connection information
 * @param error_fn XXX: not used, may be NULL
 * @param cb_arg: XXX: not used, argument for error_fn
 * @return the evsql context handle for use with other functions
 */
struct evsql *evsql_new_pq (struct event_base *ev_base, const char *pq_conninfo, 
    evsql_error_cb error_fn, 
    void *cb_arg
);

/**
 * Close the evsql handle. IMPORTANT: There are severe restrictions on the use of this function. It must *NOT* be
 * called from any evsql_*_cb callback, or the program will probably crash after the callback returns.
 *
 * Currently, the evsql handle can only be free'd if the entire evsql is idle, so this will silently abort any
 * pending queries and transactions, which may lead to nasty things.
 *
 * As a workaround to the callback-issue, you can call evsql_destroy from the next event loop iteration, which is
 * what evsql_destroy_next does for you.
 *
 * @param evsql the context handle from \ref evsql_new_
 */
void evsql_destroy (struct evsql *evsql);

/**
 * Call evsql_destroy in the next event loop iteration. If scheduling this fails, we return -1 (not a meaningful error
 * code, but nonzero).
 */
evsql_err_t evsql_destroy_next (struct evsql *evsql);

// @}

/**
 * Query API
 *
 * @defgroup evsql_query_* Query interface
 * @see evsql.h
 * @{
 */

/**
 * Queue the given query for execution.
 *
 * If \a trans is given (i.e. not NULL), then the transaction must be idle, and the query will be executed in that
 * transaction's context. Otherwise, the query will be executed without a transaction using an idle connection, or
 * enqueued for later execution.
 *
 * Once the query is complete (got a result, got an error, the connection failed), then \a query_fn will be called.
 * The callback can use the \ref evsql_result_ functions to manipulate the query results.
 *
 * The returned evsql_query handle can be passed to evsql_query_abort at any point before \a query_fn being called. 
 *
 * @param evsql the context handle from \ref evsql_new_
 * @param trans the optional transaction handle from evsql_trans
 * @param command the raw SQL command itself
 * @param query_fn the evsql_query_cb() to call once the query is complete
 * @param cb_arg the void* passed to the above
 * @return the evsql_query handle that can be used to abort the query
 */
struct evsql_query *evsql_query (struct evsql *evsql, struct evsql_trans *trans, const char *command, evsql_query_cb query_fn, void *cb_arg);

/**
 * Execute the given SQL query using the list of parameter types/values given via evsql_query_params.
 *
 * Use the EVSQL_PARAMS macros to declare \a params, and the \ref evsql_param_ functions to populate the values.
 *
 * See evsql_query() for more info about behaviour.
 *
 * See the <a href="http://www.postgresql.org/docs/8.3/static/libpq-exec.html#LIBPQ-EXEC-MAIN">libpq PQexecParams tip</a>
 * for the parameter syntax to use.
 *
 * @param evsql the context handle from \ref evsql_new_
 * @param trans the optional transaction handle from evsql_trans
 * @param command the SQL command to bind the parameters to
 * @param params the parameter types and values
 * @param query_fn the evsql_query_cb() to call once the query is complete
 * @param cb_arg the void* passed to the above
 * @see evsql_query
 */
struct evsql_query *evsql_query_params (struct evsql *evsql, struct evsql_trans *trans, 
    const char *command, const struct evsql_query_params *params, 
    evsql_query_cb query_fn, void *cb_arg
);

/**
 * Execute the given \a query_info's SQL query with the values given as variable arguments, using the \a query_info to
 * resolve the types.
 *
 * See evsql_query() for more info about behaviour.
 *
 * @param evsql the context handle from \ref evsql_new_
 * @param trans the optional transaction handle from evsql_trans
 * @param query_info the SQL query information
 * @param query_fn the evsql_query_cb() to call once the query is complete
 * @param cb_arg the void* passed to the above
 * @see evsql_query
 */
struct evsql_query *evsql_query_exec (struct evsql *evsql, struct evsql_trans *trans, 
    const struct evsql_query_info *query_info,
    evsql_query_cb query_fn, void *cb_arg,
    ...
);

/**
 * Abort a \a query returned by \ref evsql_query_ that has not yet completed (query_fn has not been called yet).
 *
 * The actual query itself may or may not be aborted (and hence may or may not be executed on the server), but \a query_fn
 * will not be called anymore, and the query will dispose of itself and any results returned.
 *
 * If the \a query is part of a transaction, then \a trans must be given, and the query must be the query that is currently
 * executing on that trans. The transaction's \a ready_fn will be called once the query has been aborted and the
 * transaction is now idle again.
 *
 * @param trans if the query is part of a transaction, then it MUST be given here
 * @param query the in-progress query to abort
 */
void evsql_query_abort (struct evsql_trans *trans, struct evsql_query *query);

/**
 * Print out a textual dump of the given \a sql query and \a params using DEBUG
 *
 * @param sql the SQL query command
 * @param params the list of parameter types and values
 */
void evsql_query_debug (const char *sql, const struct evsql_query_params *params);

// @}

/**
 * Transaction API
 *
 * @defgroup evsql_trans_* Transaction interface
 * @see evsql.h
 * @{
 */

/**
 * Create a new transaction.
 *
 * A transaction will be allocated its own connection, and the "BEGIN TRANSACTION ..." query will be sent (use the
 * \a type argument to specify this). 
 *
 * Once the transaction has been opened, the given \a ready_fn will be triggered, and the transaction can then
 * be used (see \ref evsql_query_).
 *
 * If, at any point, the transaction-connection fails, any pending query will be forgotten (i.e. the query callback
 * will NOT be called), and the given \a error_fn will be called. Note that this includes some, but not all,
 * cases where \ref evsql_query_ returns an error.
 *
 * Once you are done with the transaction, call either evsql_trans_commit() or evsql_trans_abort().
 *
 * @param evsql the context handle from \ref evsql_new_
 * @param type the type of transaction to create
 * @param error_fn the evsql_trans_error_cb() to call if this transaction fails
 * @param ready_fn the evsql_trans_ready_cb() to call once this transaction is ready for use
 * @param done_fn the evsql_trans_done_cb() to call once this transaction has been commmited
 * @param cb_arg the void* to pass to the above
 * @return the evsql_trans handle for use with other functions
 */
struct evsql_trans *evsql_trans (struct evsql *evsql, enum evsql_trans_type type, 
    evsql_trans_error_cb error_fn, 
    evsql_trans_ready_cb ready_fn, 
    evsql_trans_done_cb done_fn, 
    void *cb_arg
);

/**
 * Commit a transaction using "COMMIT TRANSACTION".
 *
 * The transaction must be idle, just like for evsql_query. Once the transaction has been commited, the transaction's
 * \a done_fn will be called, after which the transaction must not be used anymore.
 *
 * You cannot abort a COMMIT, calling trans_abort() on trans after a succesful trans_commit is an error.
 *
 * Note that \a done_fn will never be called directly, always indirectly via the event loop.
 *
 * @param trans the transaction handle from evsql_trans to commit
 * @see evsql_trans
 */
int evsql_trans_commit (struct evsql_trans *trans);

/**
 * Abort a transaction, using "ROLLBACK TRANSACTION".
 *
 * No more transaction callbacks will be called. If there was a query running, it will be aborted, and the transaction
 * then rollback'd.
 *
 * You cannot abort a COMMIT, calling trans_abort on \a trans after a call to trans_commit is an error.
 * 
 * Do not call evsql_trans_abort from within evsql_trans_error_cb()!
 *
 * @param trans the transaction from evsql_trans to abort
 * @see evsql_trans
 */
void evsql_trans_abort (struct evsql_trans *trans);

/** 
 * Retrieve the transaction-specific error message from the underlying engine.
 *
 * Intended to be called from evsql_trans_error_cb()
 */
const char *evsql_trans_error (struct evsql_trans *trans);

// @}

/**
 * Parameter-building functions.
 *
 * These manipulate the value of the given parameter index.
 *
 * @defgroup evsql_param_* Parameter interface
 * @see evsql.h
 * @{
 */

/**
 * Sets the value of the parameter at the given index
 *
 * @param params the evsql_query_params struct
 * @param param the parameter index
 * @param ptr pointer to the binary data
 * @param len size of the binary data in bytes
 * @return zero on success, <0 on error
 */
int evsql_param_binary (struct evsql_query_params *params, size_t param, const char *ptr, size_t len);

/** @see evsql_param_binary */
int evsql_param_string (struct evsql_query_params *params, size_t param, const char *ptr);

/** @see evsql_param_binary */
int evsql_param_uint16 (struct evsql_query_params *params, size_t param, uint16_t uval);

/** @see evsql_param_binary */
int evsql_param_uint32 (struct evsql_query_params *params, size_t param, uint32_t uval);

/**
 * Sets the given parameter to NULL
 *
 * @param params the evsql_query_params struct
 * @param param the parameter index
 * @return zero on success, <0 on error
 */
int evsql_param_null (struct evsql_query_params *params, size_t param);

/**
 * Clears all the parameter values (sets them to NULL)
 *
 * @param params the evsql_query_params struct
 * @return zero on success, <0 on error
 */
int evsql_params_clear (struct evsql_query_params *params);

// @}

/**
 * Result-handling functions
 *
 * @defgroup evsql_result_* Result interface
 * @see evsql.h
 * @see evsql_result
 * @{
 */

/**
 * Check the result for errors. Intended for use with non-data queries, i.e. CREATE, etc.
 *
 * Returns zero if the query was OK, err otherwise. EIO indicates an SQL error, the error message can be retrived
 * using evsql_result_error.
 *
 * @param res the result handle passed to evsql_query_cb()
 * @return zero on success, EIO on SQL error, positive error code otherwise
 */
evsql_err_t evsql_result_check (struct evsql_result *res);

/**
 * The iterator-based interface results interface.
 *
 * Define an evsql_result_info struct that describes the columns returned by the query, and call evsql_result_begin on
 * the evsql_result. This verifies the query result, and then prepares it for iteration using evsql_result_next.
 *
 * Call evsql_result_end once you've stopped iteration.
 *
 * Returns zero if the evsql_result is ready for iteration, err otherwise. EIO indicates an SQL error, the error
 * message can be retreived using evsql_result_error. The result must be released in both cases.
 *
 * Note: currently the iterator state is simply stored in evsql_result, so only one iterator at a time per evsql_result.
 *
 * @param info the metadata to use to handle the result row columns
 * @param res the result handle passed to evsql_query_cb()
 * @return zero on success, +err on error
 */
evsql_err_t evsql_result_begin (struct evsql_result_info *info, struct evsql_result *res);

/**
 * Reads the next result row from the result prepared using evsql_result_begin. Stores the field values into to given
 * pointer arguments based on the evsql_result_info given to evsql_result_begin.
 *
 * If a field is NULL, and the result_info's evsql_item_type has flags.null_ok set, the given pointer is left
 * untouched, otherwise, an error is returned.
 *
 * @param res the result handle previous prepared using evsql_result_begin
 * @param ... a set of pointers corresponding to the evsql_result_info specified using evsql_result_begin
 * @return >0 when a row was read, zero when there are no more rows left, and -err on error
 */
int evsql_result_next (struct evsql_result *res, ...);

/**
 * Ends the result iteration, releasing any associated resources and the result itself.
 *
 * The result should not be iterated or accessed anymore.
 *
 * Note: this does the same thing as evsql_result_free, and works regardless of evsql_result_begin returning
 * succesfully or not.
 *
 * @param res the result handle passed to evsql_query_cb()
 * @see evsql_result_free
 */
void evsql_result_end (struct evsql_result *res);

/**
 * Get the error message associated with the result, intended for use after evsql_result_check/begin return an error
 * code.
 * 
 * @param res the result handle passed to evsql_query_cb()
 * @return a char* containing the NUL-terminated error string. Valid until evsql_result_free is called.
 */
const char *evsql_result_error (const struct evsql_result *res);

/**
 * Get the number of data rows returned by the query
 *
 * @param res the result handle passed to evsql_query_cb()
 * @return the number of rows, >= 0
 */
size_t evsql_result_rows (const struct evsql_result *res);

/**
 * Get the number of columns in the data results from the query
 *
 * @param res the result handle passed to evsql_query_cb()
 * @return the number of columns, presumeably zero if there were no results
 */
size_t evsql_result_cols (const struct evsql_result *res);

/**
 * Get the number of rows affected by an UPDATE/INSERT/etc query.
 *
 * @param res the result handle passed to evsql_query_cb()
 * @return the number of rows affected, >= 0
 */
size_t evsql_result_affected (const struct evsql_result *res);

/**
 * Fetch the raw binary value for the given field, returning it via ptr/size.
 *
 * The given row/col must be within bounds as returned by evsql_result_rows/cols.
 *
 * *ptr will point to *size bytes of read-only memory allocated internally.
 *
 * @param res the result handle passed to evsql_query_cb()
 * @param row the row index to access
 * @param col the column index to access
 * @param ptr where to store a pointer to the read-only field data, free'd upon evsql_result_free
 * @param size updated to the size of the field value pointed to by ptr
 * @param nullok when true and the field value is NULL, *ptr and *size are not modified, otherwise NULL means an error
 * @return zero on success, <0 on error
 */
int evsql_result_binary (const struct evsql_result *res, size_t row, size_t col, const char **ptr, size_t *size, bool nullok);

/**
 * Fetch the textual value of the given field, returning it via ptr.
 *
 * The given row/col must be within bounds as returned by evsql_result_rows/cols.
 *
 * *ptr will point to a NUL-terminated string allocated internally.
 *
 * @param res the result handle passed to evsql_query_cb()
 * @param row the row index to access
 * @param col the column index to access
 * @param ptr where to store a pointer to the read-only field data, free'd upon evsql_result_free
 * @param nullok when true and the field value is NULL, *ptr and *size are not modified, otherwise NULL means an error
 * @return zero on success, <0 on error
 */
int evsql_result_string (const struct evsql_result *res, size_t row, size_t col, const char **ptr, int nullok);

/**
 * Use evsql_result_binary to read a binary field value, and then convert it using ntoh[slq], storing the value in
 * *val.
 *
 * The given row/col must be within bounds as returned by evsql_result_rows/cols.
 *
 * @param res the result handle passed to evsql_query_cb()
 * @param row the row index to access
 * @param col the column index to access
 * @param uval where to store the decoded value
 * @param nullok when true and the field value is NULL, *ptr and *size are not modified, otherwise NULL means an error
 * @return zero on success, <0 on error
 */
int evsql_result_uint16 (const struct evsql_result *res, size_t row, size_t col, uint16_t *uval, int nullok);

/** @see evsql_result_uint16 */
int evsql_result_uint32 (const struct evsql_result *res, size_t row, size_t col, uint32_t *uval, int nullok);

/** @see evsql_result_uint16 */
int evsql_result_uint64 (const struct evsql_result *res, size_t row, size_t col, uint64_t *uval, int nullok);

/**
 * Every result handle passed to evsql_query_cb() MUST be released by the user, using this function.
 *
 * @param res the result handle passed to evsql_query_cb()
 */
void evsql_result_free (struct evsql_result *res);

// @}

#endif /* EVSQL_H */
