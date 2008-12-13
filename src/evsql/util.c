#include <stdlib.h>
#include <assert.h>

#include "evsql.h"
#include "../lib/log.h"
#include "../lib/misc.h"

#define _PARAM_TYPE_CASE(typenam) case EVSQL_TYPE_ ## typenam: return #typenam

#define _PARAM_VAL_BUF_MAX 120
#define _PARAM_VAL_CASE(typenam, ...) case EVSQL_TYPE_ ## typenam: if (item->bytes) ret = snprintf(buf, _PARAM_VAL_BUF_MAX, __VA_ARGS__); else return "(null)"; break

const char *evsql_item_type (const struct evsql_item_info *item_info) {
    switch (item_info->type) {
        _PARAM_TYPE_CASE (INVALID   );
        _PARAM_TYPE_CASE (NULL_     );
        _PARAM_TYPE_CASE (BINARY    );
        _PARAM_TYPE_CASE (STRING    );
        _PARAM_TYPE_CASE (UINT16    );
        _PARAM_TYPE_CASE (UINT32    );
        _PARAM_TYPE_CASE (UINT64    );
        default: return "???";
    }
}


static const char *evsql_item_val (const struct evsql_item *item) {
    static char buf[_PARAM_VAL_BUF_MAX];
    int ret;

    switch (item->info.type) {
        _PARAM_VAL_CASE (INVALID,   "???"                               );
        _PARAM_VAL_CASE (NULL_,     "(null)"                            );
        _PARAM_VAL_CASE (BINARY,    "%zu:%s",   item->length, "... "    );
        _PARAM_VAL_CASE (STRING,    "%s",       item->bytes             );
        _PARAM_VAL_CASE (UINT16,    "%hu",      (unsigned short int)     ntohs(item->value.uint16)  );
        _PARAM_VAL_CASE (UINT32,    "%lu",      (unsigned long int)      ntohl(item->value.uint32)  );
        _PARAM_VAL_CASE (UINT64,    "%llu",     (unsigned long long int) ntohq(item->value.uint64)  );
        default: return "???";
    }

    return buf;
}

int evsql_params_clear (struct evsql_query_params *params) {
    struct evsql_item *param;

    for (param = params->list; param->info.type; param++) {
        param->bytes = NULL;
        param->flags.has_value = 0;
    }

    return 0;
}

int evsql_param_null (struct evsql_query_params *params, size_t param) {
    struct evsql_item *p = &params->list[param];

    p->bytes = NULL;
    p->flags.has_value = 0;

    return 0;
}

int evsql_param_binary (struct evsql_query_params *params, size_t param, const char *ptr, size_t len) {
    struct evsql_item *p = &params->list[param];
    
    assert(p->info.type == EVSQL_TYPE_BINARY);

    p->bytes = ptr;
    p->length = len;

    return 0;
}

int evsql_param_string (struct evsql_query_params *params, size_t param, const char *ptr) {
    struct evsql_item *p = &params->list[param];
    
    assert(p->info.type == EVSQL_TYPE_STRING);
    
    // XXX: hmm...
    p->info.format = EVSQL_FMT_TEXT;

    p->bytes = ptr;
    p->length = 0;

    return 0;
}

int evsql_param_uint16 (struct evsql_query_params *params, size_t param, uint16_t uval) {
    struct evsql_item *p = &params->list[param];
    
    assert(p->info.type == EVSQL_TYPE_UINT16);

    p->value.uint16 = htons(uval);
    p->length = sizeof(uval);
    p->flags.has_value = 1;

    return 0;
}

int evsql_param_uint32 (struct evsql_query_params *params, size_t param, uint32_t uval) {
    struct evsql_item *p = &params->list[param];
    
    assert(p->info.type == EVSQL_TYPE_UINT32);

    p->value.uint32 = htonl(uval);
    p->length = sizeof(uval);
    p->flags.has_value = 1;

    return 0;
}

void evsql_query_debug (const char *sql, const struct evsql_query_params *params) {
    const struct evsql_item *param;
    size_t param_count = 0, idx = 0;

    // count the params
    for (param = params->list; param->info.type; param++) 
        param_count++;
    
    DEBUG("sql:     %s", sql);
    DEBUG("params:  %zu", param_count);

    for (param = params->list; param->info.type; param++) {
        DEBUG("\t%2zu : %8s = %s", ++idx, evsql_item_type(&param->info), evsql_item_val(param));
    }
}

int evsql_result_binary (const struct evsql_result *res, size_t row, size_t col, const char **ptr, size_t *size, bool nullok) {
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
    
            *size = PQgetlength(res->result.pq, row, col);
            *ptr  = PQgetvalue(res->result.pq, row, col);

            return 0;

        default:
            FATAL("res->evsql->type");
    }

error:
    return -1;
}

int evsql_result_binlen (const struct evsql_result *res, size_t row, size_t col, const char **ptr, size_t size, int nullok) {
    size_t real_size = 0;

    if (evsql_result_binary(res, row, col, ptr, &real_size, nullok))
        goto error;
    
    if (*ptr == NULL) {
        assert(nullok);
        return 0;
    }

    if (size && real_size != size)
        ERROR("[%zu:%zu] field size mismatch: %zu -> %zu", row, col, size, real_size);
     
    return 0;

error:
    return -1;
}

int evsql_result_string (const struct evsql_result *res, size_t row, size_t col, const char **ptr, int nullok) {
    size_t real_size;

    if (evsql_result_binary(res, row, col, ptr, &real_size, nullok))
        goto error;

    assert(real_size == strlen(*ptr));
    
    return 0;

error:
    return -1;
}

int evsql_result_uint16 (const struct evsql_result *res, size_t row, size_t col, uint16_t *uval, int nullok) {
    const char *data;
    int16_t sval;

    if (evsql_result_binlen(res, row, col, &data, sizeof(*uval), nullok))
        goto error;
    
    if (!data)
        return 0;

    sval = ntohs(*((int16_t *) data));

    if (sval < 0)
        ERROR("negative value for unsigned: %d", sval);

    *uval = sval;
    
    return 0;

error:
    return nullok ? 0 : -1;
}

int evsql_result_uint32 (const struct evsql_result *res, size_t row, size_t col, uint32_t *uval, int nullok) {
    const char *data;
    int32_t sval;

    if (evsql_result_binlen(res, row, col, &data, sizeof(*uval), nullok))
        goto error;
    
    if (!data)
        return 0;

    sval = ntohl(*(int32_t *) data);

    if (sval < 0)
        ERROR("negative value for unsigned: %d", sval);

    *uval = sval;
    
    return 0;

error:
    return nullok ? 0 : -1;
}

int evsql_result_uint64 (const struct evsql_result *res, size_t row, size_t col, uint64_t *uval, int nullok) {
    const char *data;
    int64_t sval;

    if (evsql_result_binlen(res, row, col, &data, sizeof(*uval), nullok))
        goto error;
    
    if (!data)
        return 0;

    sval = ntohq(*(int64_t *) data);

    if (sval < 0)
        ERROR("negative value for unsigned: %ld", sval);

    *uval = sval;
    
    return 0;

error:
    return nullok ? 0 : -1;
}

const char *evsql_conn_error (struct evsql_conn *conn) {
    switch (conn->evsql->type) {
        case EVSQL_EVPQ:
            if (!conn->engine.evpq)
                return "unknown error (no conn)";
            
            return evpq_error_message(conn->engine.evpq);

        default:
            FATAL("res->evsql->type");
    }
}

const char *evsql_trans_error (struct evsql_trans *trans) {
    if (trans->conn == NULL)
        return "unknown error (no trans conn)";

    return evsql_conn_error(trans->conn);
}

