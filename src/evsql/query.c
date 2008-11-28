
#include "evsql.h"
#include "../lib/error.h"
#include "../lib/misc.h"

#include <stdlib.h>
#include <assert.h>

/*
 * Initialize params->types/values/lengths/formats, params->count, params->result_format based on the given args
 */
static int _evsql_query_params_init_pq (struct evsql_query_params_pq *params, size_t param_count, enum evsql_item_format result_format) {
    // set count
    params->count = param_count;

    // allocate vertical storage for the parameters
    if (0
        
        ||  !(params->types     = calloc(param_count, sizeof(Oid)))
        ||  !(params->values    = calloc(param_count, sizeof(char *)))
        ||  !(params->lengths   = calloc(param_count, sizeof(int)))
        ||  !(params->formats   = calloc(param_count, sizeof(int)))
        ||  !(params->item_vals = calloc(param_count, sizeof(union evsql_item_value)))
    )
        ERROR("calloc");

    // result format
    switch (result_format) {
        case EVSQL_FMT_TEXT:
            params->result_format = 0; break;

        case EVSQL_FMT_BINARY:
            params->result_format = 1; break;

        default:
            FATAL("params.result_fmt: %d", result_format);
    }
    
    // good
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

struct evsql_query *evsql_query_params (struct evsql *evsql, struct evsql_trans *trans, 
    const char *command, const struct evsql_query_params *params, 
    evsql_query_cb query_fn, void *cb_arg
) {
    struct evsql_query *query = NULL;
    const struct evsql_item *param;
    size_t count = 0, idx;
    
    // alloc new query
    if ((query = _evsql_query_new(evsql, trans, query_fn, cb_arg)) == NULL)
        goto error;

    // count the params
    for (param = params->list; param->info.type; param++) 
        count++;
    
    // initialize params
    _evsql_query_params_init_pq(&query->params, count, params->result_format);

    // transform
    for (param = params->list, idx = 0; param->info.type; param++, idx++) {
        // `set for NULLs, otherwise not
        query->params.types[idx] = param->bytes ? 0 : EVSQL_PQ_ARBITRARY_TYPE_OID;

        // scalar values
        query->params.item_vals[idx] = param->value;
        
        // values
        // point this at the value stored in the item_vals union if flagged as such
        query->params.values[idx] = param->flags.has_value ? (const char *) &query->params.item_vals[idx] : param->bytes;

        // lengths
        query->params.lengths[idx] = param->length;

        // formats, binary if length is nonzero, but text for NULLs
        query->params.formats[idx] = param->length && param->bytes ? 1 : 0;
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

struct evsql_query *evsql_query_exec (struct evsql *evsql, struct evsql_trans *trans, 
    const struct evsql_query_info *query_info,
    evsql_query_cb query_fn, void *cb_arg,
    ...
) {
    va_list vargs;
    struct evsql_query *query = NULL;
    const struct evsql_item_info *param;
    size_t count = 0, idx;
    err_t err = 1;

    // varargs
    va_start(vargs, cb_arg);
    
    // alloc new query
    if ((query = _evsql_query_new(evsql, trans, query_fn, cb_arg)) == NULL)
        goto error;

    // count the params
    for (param = query_info->params; param->type; param++) 
        count++;
    
    // initialize params
    _evsql_query_params_init_pq(&query->params, count, EVSQL_FMT_BINARY);

    // transform
    for (param = query_info->params, idx = 0; param->type; param++, idx++) {
        // default type to 0 (implicit)
        query->params.types[idx] = 0;

        // default format to binary
        query->params.formats[idx] = EVSQL_FMT_BINARY;

        // consume argument
        switch (param->type) {
            case EVSQL_TYPE_NULL_: {
                // explicit type + text fmt
                query->params.types[idx] = EVSQL_PQ_ARBITRARY_TYPE_OID;
                query->params.values[idx] = NULL;
                query->params.lengths[idx] = 0;
                query->params.formats[idx] = EVSQL_FMT_TEXT;
            } break;

            case EVSQL_TYPE_BINARY: {
                struct evsql_item_binary item = va_arg(vargs, struct evsql_item_binary);
                
                // value + explicit len
                query->params.values[idx] = item.ptr;
                query->params.lengths[idx] = item.len;
            } break;

            case EVSQL_TYPE_STRING: {
                const char *str = va_arg(vargs, const char *);

                // value + automatic length, text format
                query->params.values[idx] = str;
                query->params.lengths[idx] = 0;
                query->params.formats[idx] = EVSQL_FMT_TEXT;
            } break;
            
            case EVSQL_TYPE_UINT16: {
                // XXX: uint16_t is passed as `int'?
                uint16_t uval = va_arg(vargs, int);

                if (uval != (int16_t) uval)
                    ERROR("param $%zu: uint16 overflow: %d", idx + 1, uval);
                
                // network-byte-order value + explicit len
                query->params.item_vals[idx].uint16 = htons(uval);
                query->params.values[idx] = (const char *) &query->params.item_vals[idx];
                query->params.lengths[idx] = sizeof(uint16_t);
            } break;
            
            case EVSQL_TYPE_UINT32: {
                uint32_t uval = va_arg(vargs, uint32_t);

                if (uval != (int32_t) uval)
                    ERROR("param $%zu: uint32 overflow: %ld", idx + 1, (unsigned long) uval);
                
                // network-byte-order value + explicit len
                query->params.item_vals[idx].uint32 = htonl(uval);
                query->params.values[idx] = (const char *) &query->params.item_vals[idx];
                query->params.lengths[idx] = sizeof(uint32_t);
            } break;

            case EVSQL_TYPE_UINT64: {
                uint64_t uval = va_arg(vargs, uint64_t);

                if (uval != (int64_t) uval)
                    ERROR("param $%zu: uint16 overflow: %lld", idx + 1, (unsigned long long) uval);
                
                // network-byte-order value + explicit len
                query->params.item_vals[idx].uint64 = htonq(uval);
                query->params.values[idx] = (const char *) &query->params.item_vals[idx];
                query->params.lengths[idx] = sizeof(uint64_t);
            } break;
            
            default: 
                FATAL("param $%zu: invalid type: %d", idx + 1, param->type);
        }
    }

    // execute it
    if (_evsql_query_enqueue(evsql, trans, query, query_info->sql))
        goto error;
    
    // no error, fallthrough for va_end
    err = 0;

error:
    // possible cleanup
    if (err)
        _evsql_query_free(query);
    
    // end varargs
    va_end(vargs);
    
    // return 
    return err ? NULL : query;
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

