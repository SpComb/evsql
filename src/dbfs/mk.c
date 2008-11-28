#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "trans.h"
#include "../lib/log.h"

struct dbfs_mk_ctx {
    struct dbfs_trans base;

    const char *type, *data_expr;
    char *link, *name;
    uint16_t mode;
    uint32_t ino, parent;

    unsigned char is_dir : 1;
};

// default mode for symlinks
#define DBFS_SYMLINK_MODE 0777

// max. size for an dbfs_mk INSERT query
#define DBFS_MK_SQL_MAX 512

void dbfs_mk_free (struct dbfs_trans *ctx_base) {
    struct dbfs_mk_ctx *ctx = (struct dbfs_mk_ctx *) ctx_base;
    
    free(ctx->link);
    free(ctx->name);
}

void dbfs_mk_commit (struct dbfs_trans *ctx_base) {
    struct dbfs_mk_ctx *ctx = (struct dbfs_mk_ctx *) ctx_base;
    struct fuse_entry_param e;
    int err;
    
    // build entry
    e.ino = e.attr.st_ino = ctx->ino;
    e.attr.st_mode = _dbfs_mode(ctx->type) | ctx->mode;
    e.attr.st_size = ctx->link ? strlen(ctx->link) : 0;
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

void dbfs_mk_filetree (struct evsql_result *res, void *arg) {
    struct dbfs_mk_ctx *ctx = arg;
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

void dbfs_mk_inode (struct evsql_result *res, void *arg) {
    struct dbfs_mk_ctx *ctx = arg;
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
        " INTO file_tree (name, parent, ino, ino_dir)"
        " VALUES ($1::varchar, $2::int4, $3::int4, $4::int4)";
    
    static struct evsql_query_params params = EVSQL_PARAMS(EVSQL_FMT_BINARY) {
        EVSQL_PARAM ( STRING ),
        EVSQL_PARAM ( UINT32 ),
        EVSQL_PARAM ( UINT32 ),
        EVSQL_PARAM ( UINT32 ),

        EVSQL_PARAMS_END
    };

    if (0
        ||  evsql_param_string(&params, 0, ctx->name)
        ||  evsql_param_uint32(&params, 1, ctx->parent)
        ||  evsql_param_uint32(&params, 2, ctx->ino)
        ||  ctx->is_dir ? evsql_param_uint32(&params, 3, ctx->ino) : evsql_param_null(&params, 3)
    )
        goto error;
    
    // query
    if (evsql_query_params(dbfs_ctx->db, ctx->base.trans, sql, &params, dbfs_mk_filetree, ctx))
        goto error;
    
    // fallthrough for result_free
    err = 0;

error:
    if (err)
        dbfs_trans_fail(&ctx->base, err);

    evsql_result_free(res);
}

void dbfs_mk_begin (struct dbfs_trans *ctx_base) {
    struct dbfs_mk_ctx *ctx = (struct dbfs_mk_ctx *) ctx_base;
    struct dbfs *dbfs_ctx = fuse_req_userdata(ctx_base->req);
    int ret;

    // insert inode
    char sql_buf[DBFS_MK_SQL_MAX];
    
    if ((ret = snprintf(sql_buf, DBFS_MK_SQL_MAX, 
        "INSERT"
        " INTO inodes (type, mode, data, link_path)"
        " VALUES ($1::char(3), $2::int2, %s, $3::varchar)"
        " RETURNING inodes.ino", ctx->data_expr ? ctx->data_expr : "NULL"
    )) >= DBFS_MK_SQL_MAX)
        ERROR("sql_buf is too small: %d", ret);

    static struct evsql_query_params params = EVSQL_PARAMS(EVSQL_FMT_BINARY) {
        EVSQL_PARAM ( STRING ),
        EVSQL_PARAM ( UINT16 ),
        EVSQL_PARAM ( STRING ),

        EVSQL_PARAMS_END
    };

    if (0
        || evsql_param_string(&params, 0, ctx->type)
        || evsql_param_uint16(&params, 1, ctx->mode)
        || evsql_param_string(&params, 2, ctx->link)
    )
        goto error;
    
    if (evsql_query_params(dbfs_ctx->db, ctx_base->trans, sql_buf, &params, dbfs_mk_inode, ctx) == NULL)
        goto error;
    
    return;

error:
    dbfs_trans_fail(ctx_base, EIO);
}

/*
 * It is assumed that name and link_path must be copied, but type remains useable
 */ 
void dbfs_mk (struct fuse_req *req, fuse_ino_t parent, const char *name, const char *type, uint16_t mode, const char *data_expr, const char *link, unsigned char is_dir) {
    struct dbfs_mk_ctx *ctx = NULL;

    // alloc
    if ((ctx = calloc(1, sizeof(*ctx))) == NULL)
        ERROR("calloc");

    // start trans
    if (dbfs_trans_init(&ctx->base, req))
        goto error;
 
    // callbacks
    ctx->base.free_fn = dbfs_mk_free;
    ctx->base.begin_fn = dbfs_mk_begin;
    ctx->base.commit_fn = dbfs_mk_commit;
   
    // state
    ctx->ino = 0;
    ctx->parent = parent;
    ctx->type = type;
    ctx->data_expr = data_expr;
    ctx->mode = mode;
    ctx->is_dir = is_dir;

    // copy volatile strings
    if (
            (link && (ctx->link = strdup(link)) == NULL)
        ||  (name && (ctx->name = strdup(name)) == NULL)
    )
        ERROR("strdup");
    
    // log
    INFO("[dbfs.mk %p:%p] parent=%lu, name=%s, type=%s, mode=%#04o data_expr=%s link=%s is_dir=%hhd", ctx, req, parent, name, type, mode, data_expr, link, is_dir);

    // wait
    return;

error:
    if (ctx)
        dbfs_trans_fail(&ctx->base, EIO);
}

/*
 * These are all just aliases to dbfs_mk
 */ 
void dbfs_mknod (struct fuse_req *req, fuse_ino_t parent, const char *name, mode_t mode, dev_t rdev) {
    int err;

    if ((mode & S_IFMT) != S_IFREG)
        EERROR(err = EINVAL, "mode is not REG: %#08o", mode);

    dbfs_mk(req, parent, name, "REG", mode & 07777, "lo_create(0)", NULL, 0);

    return;

error:
    if ((err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_error");
}

void dbfs_mkdir (struct fuse_req *req, fuse_ino_t parent, const char *name, mode_t mode) {
    dbfs_mk(req, parent, name, "DIR", mode, NULL, NULL, 1);
}


void dbfs_symlink (struct fuse_req *req, const char *link, fuse_ino_t parent, const char *name) {
    dbfs_mk(req, parent, name, "LNK", DBFS_SYMLINK_MODE, NULL, link, 0);
}

