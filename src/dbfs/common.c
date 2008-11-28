
#include <string.h>

#include "dbfs.h"
#include "../lib/log.h"

mode_t _dbfs_mode (const char *type) {
    if (!strcmp(type, "DIR"))
        return S_IFDIR;

    if (!strcmp(type, "REG"))
        return S_IFREG;
    
    if (!strcmp(type, "LNK"))
        return S_IFLNK;

    else {
        WARNING("[dbfs] weird mode-type: %s", type);
        return 0;
    }
}

int _dbfs_check_res (struct evsql_result *res, size_t rows, size_t cols) {
    int err = 0;

    // check if it failed
    if (evsql_result_check(res))
        NERROR(evsql_result_error(res));
        
    // not found?
    if (evsql_result_rows(res) == 0 && evsql_result_affected(res) == 0)
        SERROR(err = 1);

    // duplicate rows?
    if (rows && evsql_result_rows(res) != rows)
        ERROR("wrong number of rows returned");
    
    // correct number of columns
    if (evsql_result_cols(res) != cols)
        ERROR("wrong number of columns: %zu", evsql_result_cols(res));

    // good
    return 0;

error:
    if (!err)
        err = -1;

    return err;
}

err_t dbfs_check_result (struct evsql_result *res, size_t rows, size_t cols) {
    err_t err;

    // number of rows returned/affected
    size_t nrows = evsql_result_rows(res) ? : evsql_result_affected(res);

    // did the query fail outright?
    if (evsql_result_check(res))
        // dump error message
        NXERROR(err = EIO, evsql_result_error(res));
    
    // SELECT/DELETE/UPDATE WHERE didn't match any rows -> ENOENT
    if (nrows == 0)
        XERROR(err = ENOENT, "no rows returned/affected");
    
    // duplicate rows where one expected?
    if (rows && nrows != rows)
        XERROR(err = EIO, "wrong number of rows: %zu -> %zu", rows, nrows);
    
    // correct number of columns
    if (evsql_result_cols(res) != cols)
        XERROR(err = EIO, "wrong number of columns: %zu -> %zu", cols, evsql_result_cols(res));

    // good
    return 0;

error:
    return err;
}

int _dbfs_stat_info (struct stat *st, struct dbfs_stat_values *values) {
    INFO("\tst_mode=S_IF%s | %ho, st_nlink=%llu, st_size=%llu", values->type, values->mode, (long long unsigned int) values->nlink, (long long unsigned int) values->size);

    // convert and store
    st->st_mode = _dbfs_mode(values->type) | values->mode;
    st->st_nlink = values->nlink;
    st->st_size = values->size;
    
    // good
    return 0;
}



