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
union evsql_result {
    // libpq
    PGresult *pq;
};

struct evsql_result_info {
    struct evsql *evsql;
    
    int error;

    union evsql_result result;
};

/*
 * Callback for handling query-level errors.
 *
 * The query has completed, either succesfully or unsuccesfully (look at info.error).
 * info.result contains the result à la the evsql's type.
 */
typedef void (*evsql_query_cb)(const struct evsql_result_info *res, void *arg);

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
struct evsql_query *evsql_query_params (struct evsql *evsql, const char *command, const struct evsql_query_params *params, evsql_query_cb query_fn, void *cb_arg);

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
