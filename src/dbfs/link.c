#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "dbfs.h"
#include "trans.h"

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

struct dbfs_symlink_ctx {
    struct dbfs_trans base;

    char *link, *name;
    uint32_t ino, parent;
};

#define DBFS_SYMLINK_MODE 0777

void dbfs_symlink_free (struct dbfs_trans *ctx_base) {
    struct dbfs_symlink_ctx *ctx = (struct dbfs_symlink_ctx *) ctx_base;
    
    free(ctx->link);
    free(ctx->name);
}

void dbfs_symlink_commit (struct dbfs_trans *ctx_base) {
    struct dbfs_symlink_ctx *ctx = (struct dbfs_symlink_ctx *) ctx_base;
    struct fuse_entry_param e;
    int err;
    
    // build entry
    e.ino = e.attr.st_ino = ctx->ino;
    e.attr.st_mode = S_IFLNK | DBFS_SYMLINK_MODE;
    e.attr.st_size = strlen(ctx->link);
    e.attr.st_nlink = 1;
    e.attr_timeout = e.entry_timeout = CACHE_TIMEOUT;

    // reply
    if ((err = fuse_reply_entry(ctx_base->req, &e)))
        goto error;

    // req good
    ctx_base->req = NULL;
    
    // free
    dbfs_trans_free(ctx_base);

    // return
    return;

error:
    dbfs_trans_fail(ctx_base, err);
}

void dbfs_symlink_filetree (const struct evsql_result_info *res, void *arg) {
    struct dbfs_symlink_ctx *ctx = arg;
    int err = EIO;
    
    // check results
    if (_dbfs_check_res(res, 0, 0) < 0)
        goto error;
    
    // commit
    dbfs_trans_commit(&ctx->base);

    // fallthrough for result_free
    err = 0;

error:
    if (err)
        dbfs_trans_fail(&ctx->base, err);

    evsql_result_free(res);
}

void dbfs_symlink_inode (const struct evsql_result_info *res, void *arg) {
    struct dbfs_symlink_ctx *ctx = arg;
    struct dbfs *dbfs_ctx = fuse_req_userdata(ctx->base.req);
    int err = EIO;
    
    // check result
    if ((err = _dbfs_check_res(res, 1, 1)))
        SERROR(err = err > 0 ? ENOENT : EIO);
    
    // get ino
    if (evsql_result_uint32(res, 0, 0, &ctx->ino, 0))
        goto error;

    // insert file_tree entry
    const char *sql = 
        "INSERT"
        " INTO file_tree (name, parent, ino)"
        " VALUES ($1::varchar, $2::int4, $3::int4)";
    
    static struct evsql_query_params params = EVSQL_PARAMS(EVSQL_FMT_BINARY) {
        EVSQL_PARAM ( STRING ),
        EVSQL_PARAM ( UINT32 ),
        EVSQL_PARAM ( UINT32 ),

        EVSQL_PARAMS_END
    };

    if (0
        ||  evsql_param_string(&params, 0, ctx->name)
        ||  evsql_param_uint32(&params, 1, ctx->parent)
        ||  evsql_param_uint32(&params, 2, ctx->ino)
    )
        goto error;
    
    // query
    if (evsql_query_params(dbfs_ctx->db, ctx->base.trans, sql, &params, dbfs_symlink_filetree, ctx))
        goto error;
    
    // fallthrough for result_free
    err = 0;

error:
    if (err)
        dbfs_trans_fail(&ctx->base, err);

    evsql_result_free(res);
}

void dbfs_symlink_begin (struct dbfs_trans *ctx_base) {
    struct dbfs_symlink_ctx *ctx = (struct dbfs_symlink_ctx *) ctx_base;
    struct dbfs *dbfs_ctx = fuse_req_userdata(ctx_base->req);

    // insert inode
    const char *sql = 
        "INSERT"
        " INTO inodes (type, mode, link_path)"
        " VALUES ('LNK', $1::int2, $2::varchar)"
        " RETURNING inodes.ino"; 
    
    static struct evsql_query_params params = EVSQL_PARAMS(EVSQL_FMT_BINARY) {
        EVSQL_PARAM ( UINT16 ),
        EVSQL_PARAM ( STRING ),

        EVSQL_PARAMS_END
    };

    if (0
        || evsql_param_uint16(&params, 0, DBFS_SYMLINK_MODE)
        || evsql_param_string(&params, 1, ctx->link)
    )
        goto error;
    
    if (evsql_query_params(dbfs_ctx->db, ctx_base->trans, sql, &params, dbfs_symlink_inode, ctx) == NULL)
        goto error;
    
    return;

error:
    dbfs_trans_fail(ctx_base, EIO);
}

void dbfs_symlink (struct fuse_req *req, const char *link, fuse_ino_t parent, const char *name) {
    struct dbfs_symlink_ctx *ctx = NULL;

    // alloc
    if ((ctx = calloc(1, sizeof(*ctx))) == NULL)
        ERROR("calloc");

    // start trans
    if (dbfs_trans_init(&ctx->base, req))
        goto error;
 
    // callbacks
    ctx->base.free_fn = dbfs_symlink_free;
    ctx->base.begin_fn = dbfs_symlink_begin;
    ctx->base.commit_fn = dbfs_symlink_commit;
   
    // state
    ctx->ino = 0;
    ctx->parent = parent;
    if (!((ctx->link = strdup(link)) && (ctx->name = strdup(name))))
        ERROR("strdup");
    
    // log
    INFO("[dbfs.symlink %p:%p] link=%s, parent=%lu, name=%s", ctx, req, link, parent, name);

    // wait
    return;

error:
    if (ctx)
        dbfs_trans_fail(&ctx->base, EIO);
}


