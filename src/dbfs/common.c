
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

int _dbfs_check_res (const struct evsql_result_info *res, size_t rows, size_t cols) {
    int err = 0;

    // check if it failed
    if (res->error)
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



int _dbfs_stat_info (struct stat *st, const struct evsql_result_info *res, size_t row, size_t col_offset) {
    int err = 0;
    
    uint16_t mode;
    uint32_t size = 0;  // NULL for non-REG inodes
    uint64_t nlink = 0; // will be NULL for non-GROUP BY queries
    const char *type;
    
    // extract the data
    if ((0
        ||  evsql_result_string(res, row, col_offset + 0, &type,       0 ) // inodes.type
        ||  evsql_result_uint16(res, row, col_offset + 1, &mode,       0 ) // inodes.mode
        ||  evsql_result_uint32(res, row, col_offset + 2, &size,       1 ) // size
        ||  evsql_result_uint64(res, row, col_offset + 3, &nlink,      1 ) // count(*)
    ) && (err = EIO))
        ERROR("invalid db data");

    INFO("\tst_mode=S_IF%s | %ho, st_nlink=%llu, st_size=%llu", type, mode, (long long unsigned int) nlink, (long long unsigned int) size);

    // convert and store
    st->st_mode = _dbfs_mode(type) | mode;
    st->st_nlink = nlink;
    st->st_size = size;
    
    // good
    return 0;

error:
    return err;
}



