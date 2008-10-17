
#include <stdlib.h>
#include <assert.h>

#include "trans.h"
#include "../lib/log.h"

void dbfs_trans_free (struct dbfs_trans *ctx) {
    assert(!ctx->req);
    assert(!ctx->trans);
    
    if (ctx->free_fn)
        ctx->free_fn(ctx);

    free(ctx);
}

void dbfs_trans_fail (struct dbfs_trans *ctx, int err) {
    if (ctx->req) {
        if ((err = fuse_reply_err(ctx->req, err)))
            EWARNING(err, "fuse_reply_err: request hangs");

        ctx->req = NULL;
    }

    if (ctx->trans) {
        evsql_trans_abort(ctx->trans);

        ctx->trans = NULL;
    }

    dbfs_trans_free(ctx);
}

static void dbfs_trans_error (struct evsql_trans *trans, void *arg) {
    struct dbfs_trans *ctx = arg;

    // deassociate trans
    ctx->trans = NULL;

    // log error
    INFO("\t[dbfs_trans.err %p:%p] %s", ctx, ctx->req, evsql_trans_error(trans));

    // mark
    if (ctx->err_ptr)
        *ctx->err_ptr = EIO;
    
    // fail
    dbfs_trans_fail(ctx, EIO);
}

static void dbfs_trans_ready (struct evsql_trans *trans, void *arg) {
    struct dbfs_trans *ctx = arg;

    // associate trans
    ctx->trans = trans;

    // log
    INFO("\t[dbfs_trans.ready %p:%p] -> trans=%p", ctx, ctx->req, trans);

    // trigger the callback
    ctx->begin_fn(ctx);
}

static void dbfs_trans_done (struct evsql_trans *trans, void *arg) {
    struct dbfs_trans *ctx = arg;

    // deassociate trans
    ctx->trans = NULL;

    // log
    INFO("\t[dbfs_trans.done %p:%p]", ctx, ctx->req);

    // trigger the callback
    ctx->commit_fn(ctx);
}

int dbfs_trans_init (struct dbfs_trans *ctx, struct fuse_req *req) {
    struct dbfs *dbfs_ctx = fuse_req_userdata(req);
    int err;

    // store
    ctx->req = req;

    // trans
    if ((ctx->trans = evsql_trans(dbfs_ctx->db, EVSQL_TRANS_SERIALIZABLE, dbfs_trans_error, dbfs_trans_ready, dbfs_trans_done, ctx)) == NULL)
        EERROR(err = EIO, "evsql_trans");

    // good
    return 0;

error:
    return -1;
}

struct dbfs_trans *dbfs_trans_new (struct fuse_req *req) {
    struct dbfs_trans *ctx = NULL;

    // alloc
    if ((ctx = calloc(1, sizeof(*ctx))) == NULL)
        ERROR("calloc");

    // init
    if (dbfs_trans_init(ctx, req))
        goto error;

    // good
    return ctx;
    
error:
    free(ctx);

    return NULL;
}

void dbfs_trans_commit (struct dbfs_trans *ctx) {
    int err, trans_err = 0;

    // detect errors
    ctx->err_ptr = &trans_err;
    
    // attempt commit
    if (evsql_trans_commit(ctx->trans))
        SERROR(err = EIO);
    
    // drop err_ptr
    ctx->err_ptr = NULL;

    // ok, wait for done or error
    return;

error:
    // fail if not already failed
    if (!trans_err)
        dbfs_trans_fail(ctx, err);
}


