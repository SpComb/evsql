
#include "evsql.h"
#include "../lib/error.h"
#include "../lib/misc.h"

#include <stdlib.h>
#include <assert.h>

const char *evsql_result_error (const struct evsql_result *res) {
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

size_t evsql_result_rows (const struct evsql_result *res) {
    switch (res->evsql->type) {
        case EVSQL_EVPQ:
            return PQntuples(res->result.pq);

        default:
            FATAL("res->evsql->type");
    }
}

size_t evsql_result_cols (const struct evsql_result *res) {
    switch (res->evsql->type) {
        case EVSQL_EVPQ:
            return PQnfields(res->result.pq);

        default:
            FATAL("res->evsql->type");
    }
}

size_t evsql_result_affected (const struct evsql_result *res) {
    switch (res->evsql->type) {
        case EVSQL_EVPQ:
            // XXX: errors?
            return strtol(PQcmdTuples(res->result.pq), NULL, 10);

        default:
            FATAL("res->evsql->type");
    }
}


int evsql_result_null (const struct evsql_result *res, size_t row, size_t col) {
    switch (res->evsql->type) {
        case EVSQL_EVPQ:
            return PQgetisnull(res->result.pq, row, col);

        default:
            FATAL("res->evsql->type");
    }
}

int evsql_result_field (const struct evsql_result *res, size_t row, size_t col, char ** const ptr, size_t *size) {
    *ptr = NULL;

    switch (res->evsql->type) {
        case EVSQL_EVPQ:
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

err_t evsql_result_check (struct evsql_result *res) {
    // so simple...
    return res->error ? EIO : 0;
}

err_t evsql_result_begin (struct evsql_result_info *info, struct evsql_result *res) {
    struct evsql_item_info *col;
    size_t cols = 0, nrows;
    err_t err;

    // count info columns
    for (col = info->columns; col->type; col++)
        cols++;

    // number of rows returned/affected
    nrows = evsql_result_rows(res) || evsql_result_affected(res);

    // did the query fail outright?
    if (res->error)
        // dump error message
        NXERROR(err = EIO, evsql_result_error(res));

/*
    // SELECT/DELETE/UPDATE WHERE didn't match any rows -> ENOENT
    if (nrows == 0)
        XERROR(err = ENOENT, "no rows returned/affected");
*/

    // correct number of columns
    if (evsql_result_cols(res) != cols)
        XERROR(err = EINVAL, "wrong number of columns: %zu -> %zu", cols, evsql_result_cols(res));
    
    // assign
    res->info = info;
    res->row_offset = 0;

    // good
    return 0;

error:
    return err;

}

int evsql_result_next (struct evsql_result *res, ...) {
    va_list vargs;
    struct evsql_item_info *col;
    size_t col_idx, row_idx = res->row_offset;
    err_t err;
    
    // ensure that evsql_result_begin has been called
    assert(res->info);
    
    // check if we're past the end
    if (row_idx >= evsql_result_rows(res))
        return 0;
    
    // varargs
    va_start(vargs, res);

    for (col = res->info->columns, col_idx = 0; col->type; col++, col_idx++) {
        char *value = NULL;
        size_t length = 0;
        
        // check for NULLs, then try and get the field value
        if (evsql_result_null(res, row_idx, col_idx)) {
            if (!col->flags.null_ok)
                XERROR(err = EINVAL, "r%zu:c%zu: NULL", row_idx, col_idx);

        } else if (evsql_result_field(res, row_idx, col_idx, &value, &length)) {
            SERROR(err = EINVAL);

        }
        
        // read the arg
        switch (col->type) {
            case EVSQL_TYPE_BINARY: {
                struct evsql_item_binary *item_ptr = va_arg(vargs, struct evsql_item_binary *);

                if (value) {
                    item_ptr->ptr = value;
                    item_ptr->len = length;
                }
            } break;

            case EVSQL_TYPE_STRING: {
                char **str_ptr = va_arg(vargs, char **);

                if (value) {
                    *str_ptr = value;
                }

            } break;

            case EVSQL_TYPE_UINT16: {
                uint16_t *uval_ptr = va_arg(vargs, uint16_t *);

                if (!value) break;

                if (length != sizeof(uint16_t)) XERROR(err = EINVAL, "r%zu:c%zu: wrong size for uint16_t: %zu", row_idx, col_idx, length);

                int16_t sval = ntohs(*((int16_t *) value));

                if (sval < 0) XERROR(err = ERANGE, "r%zu:c%zu: out of range for uint16_t: %hd", row_idx, col_idx, (signed short) sval);

                *uval_ptr = sval;
            } break;
            
            case EVSQL_TYPE_UINT32: {
                uint32_t *uval_ptr = va_arg(vargs, uint32_t *);

                if (!value) break;

                if (length != sizeof(uint32_t)) XERROR(err = EINVAL, "r%zu:c%zu: wrong size for uint32_t: %zu", row_idx, col_idx, length);

                int32_t sval = ntohl(*((int32_t *) value));

                if (sval < 0) XERROR(err = ERANGE, "r%zu:c%zu: out of range for uint32_t: %ld", row_idx, col_idx, (signed long) sval);

                *uval_ptr = sval;
            } break;
            
            case EVSQL_TYPE_UINT64: {
                uint64_t *uval_ptr = va_arg(vargs, uint64_t *);

                if (!value) break;

                if (length != sizeof(uint64_t)) XERROR(err = EINVAL, "r%zu:c%zu: wrong size for uint64_t: %zu", row_idx, col_idx, length);

                int64_t sval = ntohq(*((int64_t *) value));

                if (sval < 0) XERROR(err = ERANGE, "r%zu:c%zu: out of range for uint64_t: %lld", row_idx, col_idx, (signed long long) sval);

                *uval_ptr = sval;
            } break;
            
            default:
                XERROR(err = EINVAL, "r%zu:c%zu: invalid type: %d", row_idx, col_idx, col->type);
        }
    }

    // row handled succesfully
    return 1;

error:
    return -err;
}

void evsql_result_end (struct evsql_result *res) {
    // not much more to it...
    evsql_result_free(res);
}

void evsql_result_free (struct evsql_result *res) {
    // note that the result itself might be NULL...
    // in the case of internal-error results, these may be free'd multiple times!
    switch (res->evsql->type) {
        case EVSQL_EVPQ:
            if (res->result.pq)
                return PQclear(res->result.pq);

        default:
            FATAL("res->evsql->type");
    }
}


