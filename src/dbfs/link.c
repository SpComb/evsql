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

    // query info
    static struct evsql_query_info query_info = {
        .sql    =   "SELECT"
                    " inodes.ino, " DBFS_STAT_COLS
                    " FROM file_tree INNER JOIN inodes ON (file_tree.ino = inodes.ino)"
                    " WHERE file_tree.parent = $1::int4 AND file_tree.name = $2::varchar",
        
        .params =   {
            EVSQL_TYPE ( UINT32 ),
            EVSQL_TYPE ( STRING ),

            EVSQL_TYPE_END
        }
    };

    // query
    if ((query = evsql_query_exec(ctx->db, NULL, &query_info, dbfs_entry_res, req,
        (uint32_t) parent,
        (const char *) name
    )) == NULL)
        EERROR(err = EIO, "evsql_query_params");

    // handle interrupts
    fuse_req_interrupt_func(req, dbfs_interrupt_query, query);
    
    // wait
    return;

error:
    if ((err = -fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");
}

void dbfs_readlink_res (struct evsql_result *res, void *arg) {
    struct fuse_req *req = arg;
    int err = 0;
    
    uint32_t ino;
    const char *type, *link;

    // result info
    static struct evsql_result_info result_info = {
        0, {
            EVSQL_TYPE ( UINT32 ),
            EVSQL_TYPE ( STRING ),
            EVSQL_TYPE ( STRING ),

            EVSQL_TYPE_END
        }
    };

    // begin
    if ((err = evsql_result_begin(&result_info, res)))
        EERROR(err, "evsql_result_begin");

    // get the row of data
    if ((err = evsql_result_next(res,
        &ino, &type, &link
    )) <= 0)
        SERROR(err = err || ENOENT);
    
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
    
    // query info
    static struct evsql_query_info query_info = {
        .sql    =   "SELECT"
                    " inodes.ino, inodes.type, inodes.link_path"
                    " FROM inodes"
                    " WHERE inodes.ino = $1::int4",

        .params =   {
            EVSQL_TYPE ( UINT32 ),

            EVSQL_TYPE_END
        }
    };

    // query
    if ((query = evsql_query_exec(ctx->db, NULL, &query_info, dbfs_readlink_res, req,
        (uint32_t) ino
    )) == NULL)
        SERROR(err = EIO);

    // handle interrupts
    fuse_req_interrupt_func(req, dbfs_interrupt_query, query);
    
    // wait
    return;

error:
    if ((err = -fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");

}

void dbfs_unlink_res (struct evsql_result *res, void *arg) {
    struct fuse_req *req = arg;
    int err = 0;

    // check
    if ((err = evsql_result_check(res)))
        ERROR("evsql_result_check: %s", evsql_result_error(res));
    
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
    
    // query info
    static struct evsql_query_info query_info = {
        .sql    =   "DELETE"
                    " FROM file_tree"
                    " WHERE parent = $1::int4 AND name = $2::varchar",
        
        .params =   {
            EVSQL_TYPE ( UINT32 ),
            EVSQL_TYPE ( STRING ),

            EVSQL_TYPE_END
        }
    };

    // query
    if ((query = evsql_query_exec(ctx->db, NULL, &query_info, dbfs_unlink_res, req,
        (uint32_t) parent,
        (const char *) name
    )) == NULL)
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

    // query info
    static struct evsql_query_info query_info = {
        .sql    =   "SELECT ino, type, mode, size, nlink FROM dbfs_link($1::int4, $2::int4, $3::varchar)",

        .params =   {
            EVSQL_TYPE ( UINT32 ),
            EVSQL_TYPE ( UINT32 ),
            EVSQL_TYPE ( STRING ),

            EVSQL_TYPE_END
        }
    };

    // query
    if ((query = evsql_query_exec(ctx->db, NULL, &query_info, dbfs_entry_res, req,
        (uint32_t) ino,
        (uint32_t) newparent,
        (const char *) newname
    )) == NULL)
        SERROR(err = EIO);

    // handle interrupts
    fuse_req_interrupt_func(req, dbfs_interrupt_query, query);
    
    // wait
    return;

error:
    if ((err = -fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");   
}

