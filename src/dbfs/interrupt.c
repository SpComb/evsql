
#include "dbfs.h"

void dbfs_interrupt_query (struct fuse_req *req, void *query_ptr) {
    struct evsql_query *query = query_ptr;
    err_t err;

    // abort
    evsql_query_abort(NULL, query);

    // error the req
    if ((err = -fuse_reply_err(req, EINTR)))
        PWARNING("fuse_reply_err", err);
}

void _dbfs_interrupt_ctx (struct fuse_req *req, void *ctx_ptr) {
    // dereference ctx
    struct dbfs_interrupt_ctx *ctx = ctx_ptr;
    
    // just cancel query if pending
    if (ctx->query) {
        evsql_query_abort(NULL, ctx->query);
        ctx->query = NULL;
    }

    // mark as interrupted
    ctx->interrupted = 1;
}

int dbfs_interrupt_register (struct fuse_req *req, struct dbfs_interrupt_ctx *ctx) {
    // initialize
    ctx->query = NULL;
    ctx->interrupted = 0;

    // just pass over to fuse_req_interrupt_func
    fuse_req_interrupt_func(req, _dbfs_interrupt_ctx, ctx);
}
