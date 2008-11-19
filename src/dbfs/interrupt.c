
#include "dbfs.h"

void _dbfs_interrupt_reply (evutil_socket_t _unused1, short _unused2, void *req_ptr) {
    struct fuse_req *req = req_ptr;
    err_t err;
    
    // error the req
    if ((err = -fuse_reply_err(req, EINTR)))
        EWARNING(err, "fuse_reply_err");
}

void dbfs_interrupt_query (struct fuse_req *req, void *query_ptr) {
    struct dbfs *ctx = fuse_req_userdata(req);
    struct evsql_query *query = query_ptr;
    struct timeval tv;
    err_t err;

    // abort query
    evsql_query_abort(NULL, query);

    // error the req
    if ((err = -fuse_reply_err(req, EINTR)))
        EWARNING(err, "fuse_reply_err");
    
    /*
     * Due to a locking bug in libfuse (at least 2.7.4), we can't call fuse_reply_err from the interrupt function, so we must
     * schedule after this function returns.
     * /
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (event_base_once(ctx->ev_base, -1, EV_TIMEOUT, _dbfs_interrupt_reply, req, &tv))
        PWARNING("event_base_once failed, dropping req reply: %p", req);
        */
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
