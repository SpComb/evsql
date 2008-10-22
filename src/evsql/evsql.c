#define _GNU_SOURCE
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "evsql.h"
#include "../lib/log.h"
#include "../lib/error.h"
#include "../lib/misc.h"

/*
 * A couple function prototypes
 */ 
static void _evsql_pump (struct evsql *evsql, struct evsql_conn *conn);

/*
 * Actually execute the given query.
 *
 * The backend should be able to accept the query at this time.
 *
 * You should assume that if trying to execute a query fails, then the connection should also be considred as failed.
 */
static int _evsql_query_exec (struct evsql_conn *conn, struct evsql_query *query, const char *command) {
    int err;

    DEBUG("evsql.%p: exec query=%p on trans=%p on conn=%p:", conn->evsql, query, conn->trans, conn);

    switch (conn->evsql->type) {
        case EVSQL_EVPQ:
            // got params?
            if (query->params.count) {
                err = evpq_query_params(conn->engine.evpq, command,
                    query->params.count, 
                    query->params.types, 
                    query->params.values, 
                    query->params.lengths, 
                    query->params.formats, 
                    query->params.result_format
                );

            } else {
                // plain 'ole query
                err = evpq_query(conn->engine.evpq, command);
            }

            if (err) {
                if (PQstatus(evpq_pgconn(conn->engine.evpq)) != CONNECTION_OK)
                    WARNING("conn failed");
                else
                    WARNING("query failed, dropping conn as well");
            }

            break;
        
        default:
            FATAL("evsql->type");
    }

    if (!err)
        // assign the query
        conn->query = query;

    return err;
}

/*
 * Free the query and related resources, doesn't trigger any callbacks or remove from any queues.
 *
 * The command should already be taken care of (NULL).
 */
static void _evsql_query_free (struct evsql_query *query) {
    if (!query)
        return;
        
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
 * Execute the callback if res is given, and free the query.
 *
 * The query has been aborted, it will simply be freed
 */
static void _evsql_query_done (struct evsql_query *query, const struct evsql_result_info *res) {
    if (res) {
        if (query->cb_fn)
            // call the callback
            query->cb_fn(res, query->cb_arg);
        else
            WARNING("supressing cb_fn because query was aborted");
    }

    // free
    _evsql_query_free(query);
}

/*
 * XXX:
 * /
static void _evsql_destroy (struct evsql *evsql, const struct evsql_result_info *res) {
    struct evsql_query *query;
    
    // clear the queue
    while ((query = TAILQ_FIRST(&evsql->query_queue)) != NULL) {
        _evsql_query_done(query, res);
        
        TAILQ_REMOVE(&evsql->query_queue, query, entry);
    }
    
    // free
    free(evsql);
}
*/

/*
 * Free the transaction, it should already be deassociated from the query and conn.
 */
static void _evsql_trans_free (struct evsql_trans *trans) {
    // ensure we don't leak anything
    assert(trans->query == NULL);
    assert(trans->conn == NULL);
    
    // free
    free(trans);
}

/*
 * Release a connection. It should already be deassociated from the trans and query.
 *
 * Releases the engine, removes from the conn_list and frees this.
 */
static void _evsql_conn_release (struct evsql_conn *conn) {
    // ensure we don't leak anything
    assert(conn->trans == NULL);
    assert(conn->query == NULL);

    // release the engine
    switch (conn->evsql->type) {
        case EVSQL_EVPQ:
            evpq_release(conn->engine.evpq);
            break;
        
        default:
            FATAL("evsql->type");
    }
    
    // remove from list
    LIST_REMOVE(conn, entry);
    
    // catch deadlocks
    assert(!LIST_EMPTY(&conn->evsql->conn_list) || TAILQ_EMPTY(&conn->evsql->query_queue));

    // free
    free(conn);
}

/*
 * Release a transaction, it should already be deassociated from the query.
 *
 * Perform a two-way-deassociation with the conn, and then free the trans.
 */
static void _evsql_trans_release (struct evsql_trans *trans) {
    assert(trans->query == NULL);
    assert(trans->conn != NULL);

    // deassociate the conn
    trans->conn->trans = NULL; trans->conn = NULL;

    // free the trans
    _evsql_trans_free(trans);
}

/*
 * Fail a single query, this will trigger the callback and free it.
 *
 * NOTE: Only for *TRANSACTIONLESS* queries.
 */
static void _evsql_query_fail (struct evsql* evsql, struct evsql_query *query) {
    struct evsql_result_info res; ZINIT(res);
    
    // set up the result_info
    res.evsql = evsql;
    res.trans = NULL;
    res.error = 1;
    
    // finish off the query
    _evsql_query_done(query, &res);
}

/*
 * Fail a transaction, this will silently drop any query, trigger the error callback, two-way-deassociate/release the
 * conn, and then free the trans.
 */ 
static void _evsql_trans_fail (struct evsql_trans *trans) {
    if (trans->query) {
        // free the query silently
        _evsql_query_free(trans->query); trans->query = NULL;

        // also deassociate it from the conn!
        trans->conn->query = NULL;
    }

    // tell the user
    // XXX: trans is in a bad state during this call
    if (trans->error_fn)
        trans->error_fn(trans, trans->cb_arg);
    else
        WARNING("supressing error because error_fn was NULL");
 
    // deassociate and release the conn
    trans->conn->trans = NULL; _evsql_conn_release(trans->conn); trans->conn = NULL;

    // pump the queue for requests that were waiting for this connection
    _evsql_pump(trans->evsql, NULL);

    // free the trans
    _evsql_trans_free(trans);
}

/*
 * Fail a connection. If the connection is transactional, this will just call _evsql_trans_fail, but otherwise it will
 * fail any ongoing query, and then release the connection.
 */
static void _evsql_conn_fail (struct evsql_conn *conn) {
    if (conn->trans) {
        // let transactions handle their connection failures
        _evsql_trans_fail(conn->trans);

    } else {
        if (conn->query) {
            // fail the in-progress query
            _evsql_query_fail(conn->evsql, conn->query); conn->query = NULL;
        }

        // finish off the whole connection
        _evsql_conn_release(conn);
    }
}

/*
 * Processes enqueued non-transactional queries until the queue is empty, or we managed to exec a query.
 *
 * If execing a query on a connection fails, both the query and the connection are failed (in that order).
 *
 * Any further queries will then also be failed, because there's no reconnection/retry logic yet.
 *
 * This means that if conn is NULL, all queries are failed.
 */
static void _evsql_pump (struct evsql *evsql, struct evsql_conn *conn) {
    struct evsql_query *query;
    int err;
    
    // look for waiting queries
    while ((query = TAILQ_FIRST(&evsql->query_queue)) != NULL) {
        // dequeue
        TAILQ_REMOVE(&evsql->query_queue, query, entry);
        
        if (conn) {
            // try and execute it
            err = _evsql_query_exec(conn, query, query->command);
        }

        // free the command buf
        free(query->command); query->command = NULL;

        if (err || !conn) {
            if (!conn) {
                // warn when dropping queries
                WARNING("failing query becuse there are no conns");
            }

            // fail the query
            _evsql_query_fail(evsql, query);
            
            if (conn) {
                // fail the connection
                WARNING("failing the connection because a query-exec failed");

                _evsql_conn_fail(conn); conn = NULL;
            }

        } else {
            // we have succesfully enqueued a query, and we can wait for this connection to complete
            break;

        }

        // handle the rest of the queue
    }
    
    // ok
    return;
}

/*
 * Callback for a trans's 'BEGIN' query, which means the transaction is now ready for use.
 */
static void _evsql_trans_ready (const struct evsql_result_info *res, void *arg) {
    (void) arg;

    assert(res->trans);

    // check for errors
    if (res->error)
        ERROR("transaction 'BEGIN' failed: %s", evsql_result_error(res));
    
    // transaction is now ready for use
    res->trans->ready_fn(res->trans, res->trans->cb_arg);
    
    // good
    return;

error:
    _evsql_trans_fail(res->trans);
}

/*
 * The transaction's connection is ready, send the 'BEGIN' query.
 *
 * If anything fails, calls _evsql_trans_fail and returns nonzero, zero on success
 */
static int _evsql_trans_conn_ready (struct evsql *evsql, struct evsql_trans *trans) {
    char trans_sql[EVSQL_QUERY_BEGIN_BUF];
    const char *isolation_level;
    int ret;
    
    // determine the isolation_level to use
    switch (trans->type) {
        case EVSQL_TRANS_DEFAULT:
            isolation_level = NULL; break;

        case EVSQL_TRANS_SERIALIZABLE:
            isolation_level = "SERIALIZABLE"; break;

        case EVSQL_TRANS_REPEATABLE_READ:
            isolation_level = "REPEATABLE READ"; break;

        case EVSQL_TRANS_READ_COMMITTED:
            isolation_level = "READ COMMITTED"; break;

        case EVSQL_TRANS_READ_UNCOMMITTED:
            isolation_level = "READ UNCOMMITTED"; break;

        default:
            FATAL("trans->type: %d", trans->type);
    }
    
    // build the trans_sql
    if (isolation_level)
        ret = snprintf(trans_sql, EVSQL_QUERY_BEGIN_BUF, "BEGIN TRANSACTION ISOLATION LEVEL %s", isolation_level);
    else
        ret = snprintf(trans_sql, EVSQL_QUERY_BEGIN_BUF, "BEGIN TRANSACTION");
    
    // make sure it wasn't truncated
    if (ret >= EVSQL_QUERY_BEGIN_BUF)
        ERROR("trans_sql overflow: %d >= %d", ret, EVSQL_QUERY_BEGIN_BUF);
    
    // execute the query
    if (evsql_query(evsql, trans, trans_sql, _evsql_trans_ready, NULL) == NULL)
        ERROR("evsql_query");
    
    // success
    return 0;

error:
    // fail the transaction
    _evsql_trans_fail(trans);

    return -1;
}

/*
 * The evpq connection was succesfully established.
 */ 
static void _evsql_evpq_connected (struct evpq_conn *_conn, void *arg) {
    struct evsql_conn *conn = arg;

    if (conn->trans)
        // notify the transaction
        // don't care about errors
        (void) _evsql_trans_conn_ready(conn->evsql, conn->trans);
    
    else
        // pump any waiting transactionless queries
        _evsql_pump(conn->evsql, conn);
}

/*
 * Got one result on this evpq connection.
 */
static void _evsql_evpq_result (struct evpq_conn *_conn, PGresult *result, void *arg) {
    struct evsql_conn *conn = arg;
    struct evsql_query *query = conn->query;

    assert(query != NULL);

    // if we get multiple results, only return the first one
    if (query->result.evpq) {
        WARNING("[evsql] evpq query returned multiple results, discarding previous one");
        
        PQclear(query->result.evpq); query->result.evpq = NULL;
    }
    
    // remember the result
    query->result.evpq = result;
}

/*
 * No more results for this query.
 */
static void _evsql_evpq_done (struct evpq_conn *_conn, void *arg) {
    struct evsql_conn *conn = arg;
    struct evsql_query *query = conn->query;
    struct evsql_result_info res; ZINIT(res);
    
    assert(query != NULL);
    
    // set up the result_info
    res.evsql = conn->evsql;
    res.trans = conn->trans;
    
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

    // de-associate the query from the connection
    conn->query = NULL;
    
    // how we handle query completion depends on if we're a transaction or not
    if (conn->trans) {
        // we can deassign the trans's query
        conn->trans->query = NULL;

        // was an abort?
        if (!query->cb_fn)
            // notify the user that the transaction query has been aborted
            conn->trans->ready_fn(conn->trans, conn->trans->cb_arg);

        // then hand the query to the user
        _evsql_query_done(query, &res);
        
    } else {
        // a transactionless query, so just finish it off and pump any other waiting ones
        _evsql_query_done(query, &res);

        // pump the next one
        _evsql_pump(conn->evsql, conn);
    }
}

/*
 * The connection failed.
 */
static void _evsql_evpq_failure (struct evpq_conn *_conn, void *arg) {
    struct evsql_conn *conn = arg;
    
    // just fail the conn
    _evsql_conn_fail(conn);
}

/*
 * Our evpq behaviour
 */
static struct evpq_callback_info _evsql_evpq_cb_info = {
    .fn_connected       = _evsql_evpq_connected,
    .fn_result          = _evsql_evpq_result,
    .fn_done            = _evsql_evpq_done,
    .fn_failure         = _evsql_evpq_failure,
};

/*
 * Allocate the generic evsql context.
 */
static struct evsql *_evsql_new_base (struct event_base *ev_base, evsql_error_cb error_fn, void *cb_arg) {
    struct evsql *evsql = NULL;
    
    // allocate it
    if ((evsql = calloc(1, sizeof(*evsql))) == NULL)
        ERROR("calloc");

    // store
    evsql->ev_base = ev_base;
    evsql->error_fn = error_fn;
    evsql->cb_arg = cb_arg;

    // init
    LIST_INIT(&evsql->conn_list);
    TAILQ_INIT(&evsql->query_queue);

    // done
    return evsql;

error:
    return NULL;
}

/*
 * Start a new connection and add it to the list, it won't be ready until _evsql_evpq_connected is called
 */
static struct evsql_conn *_evsql_conn_new (struct evsql *evsql) {
    struct evsql_conn *conn = NULL;
    
    // allocate
    if ((conn = calloc(1, sizeof(*conn))) == NULL)
        ERROR("calloc");

    // init
    conn->evsql = evsql;
    
    // connect the engine
    switch (evsql->type) {
        case EVSQL_EVPQ:
            if ((conn->engine.evpq = evpq_connect(evsql->ev_base, evsql->engine_conf.evpq, _evsql_evpq_cb_info, conn)) == NULL)
                goto error;
            
            break;
            
        default:
            FATAL("evsql->type");
    }

    // add it to the list
    LIST_INSERT_HEAD(&evsql->conn_list, conn, entry);

    // success
    return conn;

error:
    free(conn);

    return NULL;
}

struct evsql *evsql_new_pq (struct event_base *ev_base, const char *pq_conninfo, evsql_error_cb error_fn, void *cb_arg) {
    struct evsql *evsql = NULL;
    
    // base init
    if ((evsql = _evsql_new_base (ev_base, error_fn, cb_arg)) == NULL)
        goto error;

    // store conf
    evsql->engine_conf.evpq = pq_conninfo;

    // pre-create one connection
    if (_evsql_conn_new(evsql) == NULL)
        goto error;

    // done
    return evsql;

error:
    // XXX: more complicated than this?
    free(evsql); 

    return NULL;
}

/*
 * Checks if the connection is already allocated for some other trans/query.
 *
 * Returns:
 *      0       connection idle, can be allocated
 *      >1      connection busy
 */
static int _evsql_conn_busy (struct evsql_conn *conn) {
    // transactions get the connection to themselves
    if (conn->trans)
        return 1;
    
    // if it has a query assigned, it's busy
    if (conn->query)
        return 1;

    // otherwise, it's all idle
    return 0;
}

/*
 * Checks if the connection is ready for use (i.e. _evsql_evpq_connected was called).
 *
 * The connection should not already have a query running.
 *
 * Returns 
 *  <0  the connection is not valid (failed, query in progress)
 *  0   the connection is still pending, and will become ready at some point
 *  >0  it's ready
 */
static int _evsql_conn_ready (struct evsql_conn *conn) {
    switch (conn->evsql->type) {
        case EVSQL_EVPQ: {
            enum evpq_state state = evpq_state(conn->engine.evpq);
            
            switch (state) {
                case EVPQ_CONNECT:
                    return 0;
                
                case EVPQ_CONNECTED:
                    return 1;

                case EVPQ_QUERY:
                case EVPQ_INIT:
                case EVPQ_FAILURE:
                    return -1;
                
                default:
                    FATAL("evpq_state: %d", state);
            }

        }
        
        default:
            FATAL("evsql->type: %d", conn->evsql->type);
    }
}

/*
 * Allocate a connection for use and return it via *conn_ptr, or if may_queue is nonzero and the connection pool is
 * getting full, return NULL (query should be queued).
 *
 * Note that the returned connection might not be ready for use yet (if we created a new one, see _evsql_conn_ready).
 *
 * Returns zero if a connection was found or the request should be queued, or nonzero if something failed and the
 * request should be dropped.
 */
static int _evsql_conn_get (struct evsql *evsql, struct evsql_conn **conn_ptr, int may_queue) {
    int have_nontrans = 0;
    *conn_ptr = NULL;
    
    // find a connection that isn't busy and is ready (unless the query queue is empty).
    LIST_FOREACH(*conn_ptr, &evsql->conn_list, entry) {
        // we can only have a query enqueue itself if there is a non-trans conn it can later use
        if (!(*conn_ptr)->trans)
            have_nontrans = 1;

        // skip busy conns always
        if (_evsql_conn_busy(*conn_ptr))
            continue;
        
        // accept pending conns as long as there are NO enqueued queries (might cause deadlock otherwise)
        if (_evsql_conn_ready(*conn_ptr) == 0 && TAILQ_EMPTY(&evsql->query_queue))
            break;

        // accept conns that are in a fully ready state
        if (_evsql_conn_ready(*conn_ptr) > 0)
            break;
    }
    
    // if we found an idle connection, we can just return that right away
    if (*conn_ptr)
        return 0;

    // return NULL if may_queue and we have a non-trans conn that we can, at some point, use
    if (may_queue && have_nontrans)
        return 0;
    
    // we need to open a new connection
    if ((*conn_ptr = _evsql_conn_new(evsql)) == NULL)
        goto error;

    // good
    return 0;
error:
    return -1;
}

struct evsql_trans *evsql_trans (struct evsql *evsql, enum evsql_trans_type type, evsql_trans_error_cb error_fn, evsql_trans_ready_cb ready_fn, evsql_trans_done_cb done_fn, void *cb_arg) {
    struct evsql_trans *trans = NULL;

    // allocate it
    if ((trans = calloc(1, sizeof(*trans))) == NULL)
        ERROR("calloc");

    // store
    trans->evsql = evsql;
    trans->ready_fn = ready_fn;
    trans->done_fn = done_fn;
    trans->cb_arg = cb_arg;
    trans->type = type;

    // find a connection
    if (_evsql_conn_get(evsql, &trans->conn, 0))
        ERROR("_evsql_conn_get");

    // associate the conn
    trans->conn->trans = trans;

    // is it already ready?
    if (_evsql_conn_ready(trans->conn) > 0) {
        // call _evsql_trans_conn_ready directly, it will handle cleanup (silently, !error_fn)
        if (_evsql_trans_conn_ready(evsql, trans)) {
            // return NULL directly
            return NULL;
        }

    } else {
        // otherwise, wait for the conn to be ready
         
    }
    
    // and let it pass errors to the user
    trans->error_fn = error_fn;

    // ok
    return trans;

error:
    free(trans);

    return NULL;
}

/*
 * Validate and allocate the basic stuff for a new query.
 */
static struct evsql_query *_evsql_query_new (struct evsql *evsql, struct evsql_trans *trans, evsql_query_cb query_fn, void *cb_arg) {
    struct evsql_query *query = NULL;
    
    // if it's part of a trans, then make sure the trans is idle
    if (trans && trans->query)
        ERROR("transaction is busy");

    // allocate it
    if ((query = calloc(1, sizeof(*query))) == NULL)
        ERROR("calloc");

    // store
    query->cb_fn = query_fn;
    query->cb_arg = cb_arg;

    // success
    return query;

error:
    return NULL;
}

/*
 * Handle a new query.
 *
 * For transactions this will associate the query and then execute it, otherwise this will either find an idle
 * connection and send the query, or enqueue it.
 */
static int _evsql_query_enqueue (struct evsql *evsql, struct evsql_trans *trans, struct evsql_query *query, const char *command) {
    // transaction queries are handled differently
    if (trans) {
        // it's an in-transaction query
        assert(trans->query == NULL);
        
        // assign the query
        trans->query = query;

        // execute directly
        if (_evsql_query_exec(trans->conn, query, command)) {
            // ack, fail the transaction
            _evsql_trans_fail(trans);
            
            // caller frees query
            goto error;
        }

    } else {
        struct evsql_conn *conn;
        
        // find an idle connection
        if ((_evsql_conn_get(evsql, &conn, 1)))
            ERROR("couldn't allocate a connection for the query");

        // we must enqueue if no idle conn or the conn is not yet ready
        if (conn && _evsql_conn_ready(conn) > 0) {
            // execute directly
            if (_evsql_query_exec(conn, query, command)) {
                // ack, fail the connection
                _evsql_conn_fail(conn);
                
                // make sure we don't deadlock any queries, but if this query got a conn directly, then we shouldn't
                // have any queries enqueued anyways
                assert(TAILQ_EMPTY(&evsql->query_queue));
                
                // caller frees query
                goto error;
            }

        } else {
            // copy the command for later execution
            if ((query->command = strdup(command)) == NULL)
                ERROR("strdup");
            
            // enqueue until some connection pumps the queue
            TAILQ_INSERT_TAIL(&evsql->query_queue, query, entry);
        }
    }

    // ok, good
    return 0;

error:
    return -1;
}

struct evsql_query *evsql_query (struct evsql *evsql, struct evsql_trans *trans, const char *command, evsql_query_cb query_fn, void *cb_arg) {
    struct evsql_query *query = NULL;
    
    // alloc new query
    if ((query = _evsql_query_new(evsql, trans, query_fn, cb_arg)) == NULL)
        goto error;
    
    // just execute the command string directly
    if (_evsql_query_enqueue(evsql, trans, query, command))
        goto error;

    // ok
    return query;

error:
    _evsql_query_free(query);

    return NULL;
}

struct evsql_query *evsql_query_params (struct evsql *evsql, struct evsql_trans *trans, const char *command, const struct evsql_query_params *params, evsql_query_cb query_fn, void *cb_arg) {
    struct evsql_query *query = NULL;
    const struct evsql_query_param *param;
    int idx;
    
    // alloc new query
    if ((query = _evsql_query_new(evsql, trans, query_fn, cb_arg)) == NULL)
        goto error;

    // count the params
    for (param = params->list; param->type; param++) 
        query->params.count++;

    // allocate the vertical storage for the parameters
    if (0
        
        ||  !(query->params.types    = calloc(query->params.count, sizeof(Oid)))
        ||  !(query->params.values   = calloc(query->params.count, sizeof(char *)))
        ||  !(query->params.lengths  = calloc(query->params.count, sizeof(int)))
        ||  !(query->params.formats  = calloc(query->params.count, sizeof(int)))
    )
        ERROR("calloc");

    // transform
    for (param = params->list, idx = 0; param->type; param++, idx++) {
        // `set for NULLs, otherwise not
        query->params.types[idx] = param->data_raw ? 0 : EVSQL_PQ_ARBITRARY_TYPE_OID;
        
        // values
        query->params.values[idx] = param->data_raw;

        // lengths
        query->params.lengths[idx] = param->length;

        // formats, binary if length is nonzero, but text for NULLs
        query->params.formats[idx] = param->length && param->data_raw ? 1 : 0;
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
    if (_evsql_query_enqueue(evsql, trans, query, command))
        goto error;

#ifdef DEBUG_ENABLED
    // debug it?
    DEBUG("evsql.%p: enqueued query=%p on trans=%p", evsql, query, trans);
    evsql_query_debug(command, params);
#endif /* DEBUG_ENABLED */

    // ok
    return query;

error:
    _evsql_query_free(query);
    
    return NULL;
}

void evsql_query_abort (struct evsql_trans *trans, struct evsql_query *query) {
    assert(query);

    if (trans) {
        // must be the right query
        assert(trans->query == query);
    }

    // just strip the callback and wait for it to complete as normal
    query->cb_fn = NULL;
}

void _evsql_trans_commit_res (const struct evsql_result_info *res, void *arg) {
    (void) arg;

    assert(res->trans);

    // check for errors
    if (res->error)
        ERROR("transaction 'COMMIT' failed: %s", evsql_result_error(res));
    
    // transaction is now done
    res->trans->done_fn(res->trans, res->trans->cb_arg);
    
    // release it
    _evsql_trans_release(res->trans);

    // success
    return;

error:
    _evsql_trans_fail(res->trans);
}

int evsql_trans_commit (struct evsql_trans *trans) {
    static const char *sql = "COMMIT TRANSACTION";

    if (trans->query)
        ERROR("cannot COMMIT because transaction is still busy");
    
    // query
    if (evsql_query(trans->evsql, trans, sql, _evsql_trans_commit_res, NULL) == NULL)
        goto error;
    
    // mark it as commited in case someone wants to abort it
    trans->has_commit = 1;

    // success
    return 0;

error:
    return -1;
}

void _evsql_trans_rollback_res (const struct evsql_result_info *res, void *arg) {
    (void) arg;

    assert(res->trans);

    // fail the connection on errors
    if (res->error)
        ERROR("transaction 'ROLLBACK' failed: %s", evsql_result_error(res));

    // release it
    _evsql_trans_release(res->trans);

    // success
    return;

error:
    // fail the connection too, errors are supressed
    _evsql_trans_fail(res->trans);
}

/*
 * Used as the ready_fn callback in case of abort, otherwise directly
 */
void _evsql_trans_rollback (struct evsql_trans *trans, void *unused) {
    static const char *sql = "ROLLBACK TRANSACTION";

    (void) unused;

    // query
    if (evsql_query(trans->evsql, trans, sql, _evsql_trans_rollback_res, NULL) == NULL) {
        // fail the transaction/connection
        _evsql_trans_fail(trans);
    }

}

void evsql_trans_abort (struct evsql_trans *trans) {
    // supress errors
    trans->error_fn = NULL;
    
    if (trans->has_commit) {
        // abort after commit doesn't make sense
        FATAL("transaction was already commited");
    }

    if (trans->query) {
        // gah, some query is running
        WARNING("aborting pending query");
        
        // prepare to rollback once complete
        trans->ready_fn = _evsql_trans_rollback;
        
        // abort
        evsql_query_abort(trans, trans->query);

    } else {
        // just rollback directly
        _evsql_trans_rollback(trans, NULL);

    }
}

