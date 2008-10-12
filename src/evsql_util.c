#include <assert.h>

#include "evsql.h"
#include "evsql_internal.h"
#include "lib/error.h"
#include "lib/misc.h"

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
