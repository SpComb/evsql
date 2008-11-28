
#include "../lib/log.h"

#include "dbfs.h"

void dbfs_rename_res (struct evsql_result *res, void *arg) {
    struct fuse_req *req = arg;
    int err;

    // check the results
    if ((err = _dbfs_check_res(res, 0, 0)))
        SERROR(err = (err ==  1 ? ENOENT : EIO));
    
    // just reply
    if ((err = -fuse_reply_err(req, 0)))
        EERROR(err, "fuse_reply_err");
    
    // log
    INFO("[dbfs.rename %p] -> OK", req);

    // fallthrough for result_free
    err = 0;

error:
    if (err && (err = -fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");
    
    evsql_result_free(res);
}

void dbfs_rename (struct fuse_req *req, fuse_ino_t parent, const char *name, fuse_ino_t newparent, const char *newname) {
    struct dbfs *dbfs_ctx = fuse_req_userdata(req);
    int err;
    
    INFO("[dbfs.rename %p] parent=%lu, name=%s, newparent=%lu, newname=%s", req, parent, name, newparent, newname);

    // just one UPDATE
    const char *sql = 
        "UPDATE"
        " file_tree"
        " SET parent = $1::int4, name = $2::varchar"
        " WHERE parent = $3::int4 AND name = $4::varchar";

    static struct evsql_query_params params = EVSQL_PARAMS(EVSQL_FMT_BINARY) {
        EVSQL_PARAM ( UINT32 ),
        EVSQL_PARAM ( STRING ),
        EVSQL_PARAM ( UINT32 ),
        EVSQL_PARAM ( STRING ),

        EVSQL_PARAMS_END
    };

    if (0
        ||  evsql_param_uint32(&params, 0, newparent)
        ||  evsql_param_string(&params, 1, newname)
        ||  evsql_param_uint32(&params, 2, parent)
        ||  evsql_param_string(&params, 3, name)
    )
        SERROR(err = EIO);

    // query
    if (evsql_query_params(dbfs_ctx->db, NULL, sql, &params, dbfs_rename_res, req) == NULL)
        SERROR(err = EIO);
    
    // good, wait
    return;

error:
    if ((err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");
}
