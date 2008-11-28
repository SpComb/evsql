#include "dbfs.h"

/*
 * Handling simple ino-related ops, like lookup, readlink, unlink and link
 */

#include "../lib/log.h"
#include "../lib/misc.h"

/*
 * Used for lookup and link
 */
void dbfs_entry_res (struct evsql_result *res, void *arg) {
    struct fuse_req *req = arg;
    struct fuse_entry_param e; ZINIT(e);
    err_t err = 0;
    
    uint32_t ino;
    struct dbfs_stat_values stat_values;

    // result info
    static struct evsql_result_info result_info = {
        0, {
            {   EVSQL_FMT_BINARY,   EVSQL_TYPE_UINT32   },  // inodes.ino
            DBFS_STAT_RESULT_INFO,
            {   0,                  0                   }
        }
    };

    // begin
    if ((err = evsql_result_begin(&result_info, res)))
        EERROR(err, "query failed");

    // get the one row of data
    if ((err = evsql_result_next(res, &ino, DBFS_STAT_RESULT_VALUES(&stat_values))) <= 0)
        EERROR(err = (err ? err : ENOENT), "evsql_result_next");
   
    INFO("\t[dbfs.lookup] -> ino=%u", ino);
    
    // stat attrs
    if ((err = _dbfs_stat_info(&e.attr, &stat_values)))
        goto error;
    
    // other attrs
    e.ino = e.attr.st_ino = ino;
    e.attr_timeout = CACHE_TIMEOUT;
    e.entry_timeout = CACHE_TIMEOUT;
        
    // reply
    if ((err = -fuse_reply_entry(req, &e)))
        EERROR(err, "fuse_reply_entry");

error:
    if (err && (err = -fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");

    // done
    evsql_result_end(res);
}

void dbfs_lookup (struct fuse_req *req, fuse_ino_t parent, const char *name) {
    struct dbfs *ctx = fuse_req_userdata(req);
    struct evsql_query *query;
    int err;

    INFO("[dbfs.lookup] parent=%lu name=%s", parent, name);
    
    // query and params
    const char *sql = 
        "SELECT"
        " inodes.ino, " DBFS_STAT_COLS
        " FROM file_tree INNER JOIN inodes ON (file_tree.ino = inodes.ino)"
        " WHERE file_tree.parent = $1::int4 AND file_tree.name = $2::varchar";
    
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
    if ((query = evsql_query_params(ctx->db, NULL, sql, &params, dbfs_entry_res, req)) == NULL)
        EERROR(err = EIO, "evsql_query_params");

    // handle interrupts
    fuse_req_interrupt_func(req, dbfs_interrupt_query, query);
    
    // wait
    return;

error:
    if ((err = -fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");
}

void _dbfs_readlink_res (struct evsql_result *res, void *arg) {
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
    
    INFO("\t[dbfs.readlink %p] -> ino=%lu, type=%s, link=%s", req, (unsigned long int) ino, type, link);
    
    // reply
    if ((err = -fuse_reply_readlink(req, link)))
        EERROR(err, "fuse_reply_readlink");

error:
    if (err && (err = -fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");

    // free
    evsql_result_free(res);
}

void dbfs_readlink (struct fuse_req *req, fuse_ino_t ino) {
    struct dbfs *ctx = fuse_req_userdata(req);
    struct evsql_query *query;
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
    if ((query = evsql_query_params(ctx->db, NULL, sql, &params, _dbfs_readlink_res, req)) == NULL)
        SERROR(err = EIO);

    // handle interrupts
    fuse_req_interrupt_func(req, dbfs_interrupt_query, query);
    
    // wait
    return;

error:
    if ((err = -fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");

}

#define SETERR(err_var, err_val, bool_val) ((err_var) = bool_val ? (err_val) : 0)

void dbfs_unlink_res (struct evsql_result *res, void *arg) {
    struct fuse_req *req = arg;
    int err = 0;
    
    // check the results
    // XXX: reply with ENOTEMPTY if it fails due to this inode being a dir
    if ((err = dbfs_check_result(res, 1, 0)))
        goto error;
        
    INFO("\t[dbfs.unlink %p] -> OK", req);
    
    // reply
    if ((err = -fuse_reply_err(req, 0)))
        EERROR(err, "fuse_reply_err");

error:
    if (err && (err = -fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");

    // free
    evsql_result_free(res);
}

void dbfs_unlink (struct fuse_req *req, fuse_ino_t parent, const char *name) {
    struct dbfs *ctx = fuse_req_userdata(req);
    struct evsql_query *query;
    int err;
    
    INFO("[dbfs.unlink %p] parent=%lu, name=%s", req, parent, name);

    const char *sql =
        "DELETE"
        " FROM file_tree"
        " WHERE parent = $1::int4 AND name = $2::varchar";

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
        SERROR(err = EIO);
        
    // query
    if ((query = evsql_query_params(ctx->db, NULL, sql, &params, dbfs_unlink_res, req)) == NULL)
        SERROR(err = EIO);

    // handle interrupts
    fuse_req_interrupt_func(req, dbfs_interrupt_query, query);
    
    // wait
    return;

error:
    if ((err = -fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");
}

void dbfs_link (struct fuse_req *req, fuse_ino_t ino, fuse_ino_t newparent, const char *newname) {
    struct dbfs *ctx = fuse_req_userdata(req);
    struct evsql_query *query;
    int err;
    
    INFO("[dbfs.link %p] ino=%lu, newparent=%lu, newname=%s", req, ino, newparent, newname);

    const char *sql =
        "SELECT ino, type, mode, size, nlink FROM dbfs_link($1::int4, $2::int4, $3::varchar)";

    static struct evsql_query_params params = EVSQL_PARAMS(EVSQL_FMT_BINARY) {
        EVSQL_PARAM ( UINT32 ),
        EVSQL_PARAM ( UINT32 ),
        EVSQL_PARAM ( STRING ),

        EVSQL_PARAMS_END
    };

    // build params
    if (0
        ||  evsql_param_uint32(&params, 0, ino)
        ||  evsql_param_uint32(&params, 1, newparent)
        ||  evsql_param_string(&params, 2, newname)
    )
        SERROR(err = EIO);
        
    // query
    if ((query = evsql_query_params(ctx->db, NULL, sql, &params, dbfs_entry_res, req)) == NULL)
        SERROR(err = EIO);

    // handle interrupts
    fuse_req_interrupt_func(req, dbfs_interrupt_query, query);
    
    // wait
    return;

error:
    if ((err = -fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");   
}
