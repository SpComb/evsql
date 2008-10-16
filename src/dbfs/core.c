
#include "../lib/log.h"
#include "../lib/misc.h"

#include "dbfs.h"

/*
 * Core fs functionality like lookup, getattr
 */

void _dbfs_lookup_result (const struct evsql_result_info *res, void *arg) {
    struct fuse_req *req = arg;
    struct fuse_entry_param e; ZINIT(e);
    int err = 0;
    
    uint32_t ino;
    
    // check the results
    if ((err = _dbfs_check_res(res, 1, 5)))
        SERROR(err = (err ==  1 ? ENOENT : EIO));
    
    // get the data
    if (0
        ||  evsql_result_uint32(res, 0, 0, &ino,        0 ) // inodes.ino
    )
        EERROR(err = EIO, "invalid db data");
        
    INFO("[dbfs.lookup] -> ino=%u", ino);
    
    // stat attrs
    if (_dbfs_stat_info(&e.attr, res, 0, 1))
        goto error;

    // other attrs
    e.ino = e.attr.st_ino = ino;
    e.attr_timeout = CACHE_TIMEOUT;
    e.entry_timeout = CACHE_TIMEOUT;
        
    // reply
    if ((err = fuse_reply_entry(req, &e)))
        EERROR(err, "fuse_reply_entry");

error:
    if (err && (err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");

    // free
    evsql_result_free(res);
}

void dbfs_lookup (struct fuse_req *req, fuse_ino_t parent, const char *name) {
    struct dbfs *ctx = fuse_req_userdata(req);
    int err;

    INFO("[dbfs.lookup] parent=%lu name=%s", parent, name);
    
    // query and params
    const char *sql = 
        "SELECT"
        " inodes.ino, inodes.type, inodes.mode, dbfs_lo_size(data), count(*)"
        " FROM file_tree INNER JOIN inodes ON (file_tree.inode = inodes.ino)"
        " WHERE file_tree.parent = $1::int4 AND file_tree.name = $2::varchar"
        " GROUP BY inodes.ino, inodes.type, inodes.mode, data";
    
    static struct evsql_query_params params = EVSQL_PARAMS(EVSQL_FMT_BINARY) {
        EVSQL_PARAM ( UINT32 ),
        EVSQL_PARAM ( STRING ),

        EVSQL_PARAMS_END
    };
    
    // build params
    if (0
        ||  evsql_param_uint32(&params, 0, parent)
        ||  evsql_param_string(&params, 1, name)
    )
        EERROR(err = EIO, "evsql_param_*");

    // query
    if (evsql_query_params(ctx->db, NULL, sql, &params, _dbfs_lookup_result, req) == NULL)
        EERROR(err = EIO, "evsql_query_params");

    // XXX: handle interrupts
    
    // wait
    return;

error:
    if ((err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");
}

void _dbfs_getattr_result (const struct evsql_result_info *res, void *arg) {
    struct fuse_req *req = arg;
    struct stat st; ZINIT(st);
    int err = 0;
    
    // check the results
    if ((err = _dbfs_check_res(res, 1, 4)))
        SERROR(err = (err ==  1 ? ENOENT : EIO));
        
    INFO("[dbfs.getattr %p] -> (stat follows)", req);
    
    // stat attrs
    if (_dbfs_stat_info(&st, res, 0, 0))
        goto error;

    // XXX: we don't have the ino
    st.st_ino = 0;

    // reply
    if ((err = fuse_reply_attr(req, &st, CACHE_TIMEOUT)))
        EERROR(err, "fuse_reply_entry");

error:
    if (err && (err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");

    // free
    evsql_result_free(res);
}

void dbfs_getattr (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi) {
    struct dbfs *ctx = fuse_req_userdata(req);
    int err;
    
    (void) fi;

    INFO("[dbfs.getattr %p] ino=%lu", req, ino);

    const char *sql =
        "SELECT"
        " inodes.type, inodes.mode, dbfs_lo_size(data), count(*)"
        " FROM inodes"
        " WHERE inodes.ino = $1::int4"
        " GROUP BY inodes.type, inodes.mode, data";

    static struct evsql_query_params params = EVSQL_PARAMS(EVSQL_FMT_BINARY) {
        EVSQL_PARAM ( UINT32 ),

        EVSQL_PARAMS_END
    };

    // build params
    if (0
        ||  evsql_param_uint32(&params, 0, ino)
    )
        SERROR(err = EIO);
        
    // query
    if (evsql_query_params(ctx->db, NULL, sql, &params, _dbfs_getattr_result, req) == NULL)
        SERROR(err = EIO);

    // XXX: handle interrupts
    
    // wait
    return;

error:
    if ((err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");
}

