#define _GNU_SOURCE
#include <stdlib.h>
#include <sys/queue.h>
#include <assert.h>
#include <string.h>

#include "evsql.h"
#include "evpq.h"
#include "lib/log.h"
#include "lib/error.h"
#include "lib/misc.h"

enum evsql_type {
    EVSQL_EVPQ,
};

struct evsql {
    // callbacks
    evsql_error_cb error_fn;
    void *cb_arg;

    // backend engine
    enum evsql_type type;

    union {
        struct evpq_conn *evpq;
    } engine;
    
    // list of queries running or waiting to run
    TAILQ_HEAD(evsql_queue, evsql_query) queue;
};

struct evsql_query {
    // the evsql we are querying
    struct evsql *evsql;

    // the actual SQL query, this may or may not be ours, see _evsql_query_exec
    char *command;
    
    // possible query params
    struct evsql_query_param_info {
        int count;

        Oid *types;
        const char **values;
        int *lengths;
        int *formats;

        int result_format;
    } params;

    // our callback
    evsql_query_cb cb_fn;
    void *cb_arg;

    // our position in the query list
    TAILQ_ENTRY(evsql_query) entry;

    // the result
    union {
        PGresult *evpq;
    } result;
};


/*
 * Actually execute the given query.
 *
 * The backend should be able to accept the query at this time.
 *
 * query->command must be valid during the execution of this function, but once it returns, the command is not needed
 * anymore, and should be set to NULL.
 */
static int _evsql_query_exec (struct evsql *evsql, struct evsql_query *query, const char *command) {
    switch (evsql->type) {
        case EVSQL_EVPQ:
            // got params?
            if (query->params.count) {
                return evpq_query_params(evsql->engine.evpq, command,
                    query->params.count, 
                    query->params.types, 
                    query->params.values, 
                    query->params.lengths, 
                    query->params.formats, 
                    query->params.result_format
                );

            } else {
                // plain 'ole query
                return evpq_query(evsql->engine.evpq, command);
            }
        
        default:
            FATAL("evsql->type");
    }
}

/*
 * Free the query and related resources, doesn't trigger any callbacks or remove from any queues
 */
static void _evsql_query_free (struct evsql_query *query) {
    assert(query->command == NULL);
    
    // free params if present
    free(query->params.types);
    free(query->params.values);
    free(query->params.lengths);
    free(query->params.formats);

    // free the query itself
    free(query);
}

/*
 * Dequeue the query, execute the callback, and free it.
 */
static void _evsql_query_done (struct evsql_query *query, const struct evsql_result_info *res) {
    // dequeue
    TAILQ_REMOVE(&query->evsql->queue, query, entry);
    
    if (res) 
        // call the callback
        query->cb_fn(res, query->cb_arg);
    
    // free
    _evsql_query_free(query);
}

/*
 * A query has failed, notify the user and remove it.
 */
static void _evsql_query_failure (struct evsql *evsql, struct evsql_query *query) {
    struct evsql_result_info res; ZINIT(res);

    // set up the result_info
    res.evsql = evsql;
    res.error = 1;

    // finish it off
    _evsql_query_done(query, &res);
}

/*
 * Clear every enqueued query and then free the evsql.
 *
 * If result_info is given, each query will also recieve it via their callback, and the error_fn will be called.
 */
static void _evsql_destroy (struct evsql *evsql, const struct evsql_result_info *res) {
    struct evsql_query *query;
    
    // clear the queue
    while ((query = TAILQ_FIRST(&evsql->queue)) != NULL) {
        _evsql_query_done(query, res);
        
        TAILQ_REMOVE(&evsql->queue, query, entry);
    }
    
    // do the error callback if required
    if (res)
        evsql->error_fn(evsql, evsql->cb_arg);
    
    // free
    free(evsql);
}


/*
 * Sends the next query if there are more enqueued
 */
static void _evsql_pump (struct evsql *evsql) {
    struct evsql_query *query;
    
    // look for the next query
    if ((query = TAILQ_FIRST(&evsql->queue)) != NULL) {
        // try and execute it
        if (_evsql_query_exec(evsql, query, query->command)) {
            // the query failed
            _evsql_query_failure(evsql, query);
        }

        // free the command
        free(query->command); query->command = NULL;

        // ok, then we just wait
    }
}


static void _evsql_evpq_connected (struct evpq_conn *conn, void *arg) {
    struct evsql *evsql = arg;

    // no state to update, just pump any waiting queries
    _evsql_pump(evsql);
}

static void _evsql_evpq_result (struct evpq_conn *conn, PGresult *result, void *arg) {
    struct evsql *evsql = arg;
    struct evsql_query *query;

    assert((query = TAILQ_FIRST(&evsql->queue)) != NULL);

    // if we get multiple results, only return the first one
    if (query->result.evpq) {
        WARNING("[evsql] evpq query returned multiple results, discarding previous one");
        
        PQclear(query->result.evpq); query->result.evpq = NULL;
    }
    
    // remember the result
    query->result.evpq = result;
}

static void _evsql_evpq_done (struct evpq_conn *conn, void *arg) {
    struct evsql *evsql = arg;
    struct evsql_query *query;
    struct evsql_result_info res; ZINIT(res);

    assert((query = TAILQ_FIRST(&evsql->queue)) != NULL);
    
    // set up the result_info
    res.evsql = evsql;
    
    if (query->result.evpq == NULL) {
        // if a query didn't return any results (bug?), warn and fail the query
        WARNING("[evsql] evpq query didn't return any results");

        res.error = 1;
    
    } else if (strcmp(PQresultErrorMessage(query->result.evpq), "") != 0) {
        // the query failed with some error
        res.error = 1;
        res.result.pq = query->result.evpq;

    } else {
        res.error = 0;
        res.result.pq = query->result.evpq;

    }

    // finish it off
    _evsql_query_done(query, &res);

    // pump the next one
    _evsql_pump(evsql);
}

static void _evsql_evpq_failure (struct evpq_conn *conn, void *arg) {
    struct evsql *evsql = arg;
    struct evsql_result_info result; ZINIT(result);
    
    // OH SHI...
    
    // set up the result_info
    result.evsql = evsql;
    result.error = 1;

    // finish off the whole connection
    _evsql_destroy(evsql, &result);
}

static struct evpq_callback_info _evsql_evpq_cb_info = {
    .fn_connected       = _evsql_evpq_connected,
    .fn_result          = _evsql_evpq_result,
    .fn_done            = _evsql_evpq_done,
    .fn_failure         = _evsql_evpq_failure,
};

static struct evsql *_evsql_new_base (evsql_error_cb error_fn, void *cb_arg) {
    struct evsql *evsql = NULL;
    
    // allocate it
    if ((evsql = calloc(1, sizeof(*evsql))) == NULL)
        ERROR("calloc");

    // store
    evsql->error_fn = error_fn;
    evsql->cb_arg = cb_arg;

    // init
    TAILQ_INIT(&evsql->queue);

    // done
    return evsql;

error:
    return NULL;
}

struct evsql *evsql_new_pq (struct event_base *ev_base, const char *pq_conninfo, evsql_error_cb error_fn, void *cb_arg) {
    struct evsql *evsql = NULL;
    
    // base init
    if ((evsql = _evsql_new_base (error_fn, cb_arg)) == NULL)
        goto error;

    // connect the engine
    if ((evsql->engine.evpq = evpq_connect(ev_base, pq_conninfo, _evsql_evpq_cb_info, evsql)) == NULL)
        goto error;

    // done
    return evsql;

error:
    // XXX: more complicated than this?
    free(evsql); 

    return NULL;
}

/*
 * Checks what the state of the connection is in regards to executing a query.
 *
 * Returns:
 *      <0      connection failure, query not possible
 *      0       connection idle, can query immediately
 *      1       connection busy, must queue query
 */
static int _evsql_query_busy (struct evsql *evsql) {
    switch (evsql->type) {
        case EVSQL_EVPQ: {
            enum evpq_state state = evpq_state(evsql->engine.evpq);
            
            switch (state) {
                case EVPQ_CONNECT:
                case EVPQ_QUERY:
                    return 1;
                
                case EVPQ_CONNECTED:
                    return 0;

                case EVPQ_INIT:
                case EVPQ_FAILURE:
                    return -1;
                
                default:
                    FATAL("evpq_state");
            }

        }
        
        default:
            FATAL("evsql->type");
    }
}

static struct evsql_query *_evsql_query_new (struct evsql *evsql, evsql_query_cb query_fn, void *cb_arg) {
    struct evsql_query *query;
    
    // allocate it
    if ((query = calloc(1, sizeof(*query))) == NULL)
        ERROR("calloc");

    // store
    query->evsql = evsql;
    query->cb_fn = query_fn;
    query->cb_arg = cb_arg;

    // success
    return query;

error:
    return NULL;
}

static int _evsql_query_enqueue (struct evsql *evsql, struct evsql_query *query, const char *command) {
    int busy;
    
    // check state
    if ((busy = _evsql_query_busy(evsql)) < 0)
        ERROR("connection is not valid");
    
    if (busy) {
        // copy the command for later execution
        if ((query->command = strdup(command)) == NULL)
            ERROR("strdup");

    } else {
        assert(TAILQ_EMPTY(&evsql->queue));

        // execute directly
        if (_evsql_query_exec(evsql, query, command))
            goto error;

    }
    
    // store it on the list
    TAILQ_INSERT_TAIL(&evsql->queue, query, entry);

    // ok, good
    return 0;

error:
    return -1;
}

struct evsql_query *evsql_query (struct evsql *evsql, const char *command, evsql_query_cb query_fn, void *cb_arg) {
    struct evsql_query *query = NULL;
    
    // alloc new query
    if ((query = _evsql_query_new(evsql, query_fn, cb_arg)) == NULL)
        goto error;
    
    // just execute the command string directly
    if (_evsql_query_enqueue(evsql, query, command))
        goto error;

    // ok
    return query;

error:
    _evsql_query_free(query);

    return NULL;
}

struct evsql_query *evsql_query_params (struct evsql *evsql, const char *command, const struct evsql_query_params *params, evsql_query_cb query_fn, void *cb_arg) {
    struct evsql_query *query = NULL;
    const struct evsql_query_param *param;
    int idx;
    
    // alloc new query
    if ((query = _evsql_query_new(evsql, query_fn, cb_arg)) == NULL)
        goto error;

    // count the params
    for (param = params->list; param->type; param++) 
        query->params.count++;

    // allocate the vertical storage for the parameters
    if (0
        
//            !(query->params.types    = calloc(query->params.count, sizeof(Oid)))
        ||  !(query->params.values   = calloc(query->params.count, sizeof(char *)))
        ||  !(query->params.lengths  = calloc(query->params.count, sizeof(int)))
        ||  !(query->params.formats  = calloc(query->params.count, sizeof(int)))
    )
        ERROR("calloc");

    // transform
    for (param = params->list, idx = 0; param->type; param++, idx++) {
        // `types` stays NULL
        // query->params.types[idx] = 0;
        
        // values
        query->params.values[idx] = param->data_raw;

        // lengths
        query->params.lengths[idx] = param->length;

        // formats, binary if length is nonzero
        query->params.formats[idx] = param->length ? 1 : 0;
    }

    // result format
    switch (params->result_fmt) {
        case EVSQL_FMT_TEXT:
            query->params.result_format = 0; break;

        case EVSQL_FMT_BINARY:
            query->params.result_format = 1; break;

        default:
            FATAL("params.result_fmt: %d", params->result_fmt);
    }

    // execute it
    if (_evsql_query_enqueue(evsql, query, command))
        goto error;

    // ok
    return query;

error:
    _evsql_query_free(query);
    
    return NULL;
}

int evsql_param_string (struct evsql_query_params *params, size_t param, const char *ptr) {
    struct evsql_query_param *p = &params->list[param];
    
    assert(p->type == EVSQL_PARAM_STRING);

    p->data_raw = ptr;
    p->length = 0;

    return 0;
}

int evsql_param_uint32 (struct evsql_query_params *params, size_t param, uint32_t uval) {
    struct evsql_query_param *p = &params->list[param];
    
    assert(p->type == EVSQL_PARAM_UINT32);

    p->data.uint32 = htonl(uval);
    p->data_raw = (const char *) &p->data.uint32;
    p->length = sizeof(uval);

    return 0;
}

const char *evsql_result_error (const struct evsql_result_info *res) {
    if (!res->error)
        return "No error";

    switch (res->evsql->type) {
        case EVSQL_EVPQ:
            if (!res->result.pq)
                return "unknown error (no result)";
            
            return PQresultErrorMessage(res->result.pq);

        default:
            FATAL("res->evsql->type");
    }

}

size_t evsql_result_rows (const struct evsql_result_info *res) {
    switch (res->evsql->type) {
        case EVSQL_EVPQ:
            return PQntuples(res->result.pq);

        default:
            FATAL("res->evsql->type");
    }
}

size_t evsql_result_cols (const struct evsql_result_info *res) {
    switch (res->evsql->type) {
        case EVSQL_EVPQ:
            return PQnfields(res->result.pq);

        default:
            FATAL("res->evsql->type");
    }
}

int evsql_result_binary (const struct evsql_result_info *res, size_t row, size_t col, const char **ptr, size_t size, int nullok) {
    *ptr = NULL;

    switch (res->evsql->type) {
        case EVSQL_EVPQ:
            if (PQgetisnull(res->result.pq, row, col)) {
                if (nullok)
                    return 0;
                else
                    ERROR("[%zu:%zu] field is null", row, col);
            }

            if (PQfformat(res->result.pq, col) != 1)
                ERROR("[%zu:%zu] PQfformat is not binary: %d", row, col, PQfformat(res->result.pq, col));
    
            if (size && PQgetlength(res->result.pq, row, col) != size)
                ERROR("[%zu:%zu] field size mismatch: %zu -> %d", row, col, size, PQgetlength(res->result.pq, row, col));

            *ptr = PQgetvalue(res->result.pq, row, col);

            return 0;

        default:
            FATAL("res->evsql->type");
    }

error:
    return -1;
}

int evsql_result_string (const struct evsql_result_info *res, size_t row, size_t col, const char **ptr, int nullok) {
    return evsql_result_binary(res, row, col, ptr, 0, nullok);
}

int evsql_result_uint16 (const struct evsql_result_info *res, size_t row, size_t col, uint16_t *uval, int nullok) {
    const char *data;
    int16_t sval;

    if (evsql_result_binary(res, row, col, &data, sizeof(*uval), nullok))
        goto error;

    sval = ntohs(*((int16_t *) data));

    if (sval < 0)
        ERROR("negative value for unsigned: %d", sval);

    *uval = sval;
    
    return 0;

error:
    return nullok ? 0 : -1;
}

int evsql_result_uint32 (const struct evsql_result_info *res, size_t row, size_t col, uint32_t *uval, int nullok) {
    const char *data;
    int32_t sval;

    if (evsql_result_binary(res, row, col, &data, sizeof(*uval), nullok))
        goto error;

    sval = ntohl(*(int32_t *) data);

    if (sval < 0)
        ERROR("negative value for unsigned: %d", sval);

    *uval = sval;
    
    return 0;

error:
    return nullok ? 0 : -1;
}

int evsql_result_uint64 (const struct evsql_result_info *res, size_t row, size_t col, uint64_t *uval, int nullok) {
    const char *data;
    int64_t sval;

    if (evsql_result_binary(res, row, col, &data, sizeof(*uval), nullok))
        goto error;

    sval = ntohq(*(int64_t *) data);

    if (sval < 0)
        ERROR("negative value for unsigned: %ld", sval);

    *uval = sval;
    
    return 0;

error:
    return nullok ? 0 : -1;
}

void evsql_result_free (const struct evsql_result_info *res) {
    switch (res->evsql->type) {
        case EVSQL_EVPQ:
            return PQclear(res->result.pq);

        default:
            FATAL("res->evsql->type");
    }
}
