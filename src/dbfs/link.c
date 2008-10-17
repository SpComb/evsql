#include "dbfs.h"

#include "../lib/log.h"

void _dbfs_readlink_res (const struct evsql_result_info *res, void *arg) {
    struct fuse_req *req = arg;
    int err = 0;
    
    uint32_t ino;
    const char *type, *link;

    // check the results
    if ((err = _dbfs_check_res(res, 1, 3)))
        SERROR(err = (err ==  1 ? ENOENT : EIO));
        
    // get our data
    if (0
        ||  evsql_result_uint32(res, 0, 0, &ino,        0 ) // inodes.ino
        ||  evsql_result_string(res, 0, 1, &type,       0 ) // inodes.type
        ||  evsql_result_string(res, 0, 2, &link,       1 ) // inodes.link_path
    )
        EERROR(err = EIO, "invalid db data");
    
    // is it a symlink?
    if (_dbfs_mode(type) != S_IFLNK)
        EERROR(err = EINVAL, "wrong type: %s", type);
    
    INFO("[dbfs.readlink %p] -> ino=%lu, type=%s, link=%s", req, (unsigned long int) ino, type, link);
    
    // reply
    if ((err = fuse_reply_readlink(req, link)))
        EERROR(err, "fuse_reply_readlink");

error:
    if (err && (err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");

    // free
    evsql_result_free(res);
}

void dbfs_readlink (struct fuse_req *req, fuse_ino_t ino) {
    struct dbfs *ctx = fuse_req_userdata(req);
    int err;
    
    INFO("[dbfs.readlink %p] ino=%lu", req, ino);

    const char *sql =
        "SELECT"
        " inodes.ino, inodes.type, inodes.link_path"
        " FROM inodes"
        " WHERE inodes.ino = $1::int4";

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
    if (evsql_query_params(ctx->db, NULL, sql, &params, _dbfs_readlink_res, req) == NULL)
        SERROR(err = EIO);

    // XXX: handle interrupts
    
    // wait
    return;

error:
    if ((err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");

}

