
#include <stdlib.h>

#include "dbfs.h"
#include "../dbfs.h"
#include "../lib/log.h"

static struct fuse_lowlevel_ops dbfs_llops = {

    .init           = dbfs_init,
    .destroy        = dbfs_destroy,
    
    .lookup         = dbfs_lookup,
    // .forget
    .getattr        = dbfs_getattr,
    .setattr        = dbfs_setattr,
    .readlink       = dbfs_readlink,

    .symlink        = dbfs_symlink,

    .open           = dbfs_open,
    .read           = dbfs_read,
    .write          = dbfs_write,
    .flush          = dbfs_flush,
    .release        = dbfs_release,

    .opendir        = dbfs_opendir,
    .readdir        = dbfs_readdir,
    .releasedir     = dbfs_releasedir,
};

void dbfs_init (void *userdata, struct fuse_conn_info *conn) {
    INFO("[dbfs.init] userdata=%p, conn=%p", userdata, conn);

}

void dbfs_destroy (void *arg) {
    struct dbfs *ctx = arg;
    INFO("[dbfs.destroy %p]", ctx);

    // exit libevent
    event_base_loopexit(ctx->ev_base, NULL);
}


void dbfs_sql_error (struct evsql *evsql, void *arg) {
    struct dbfs *ctx = arg;

    // AAAAAAAAAA.... panic
    WARNING("[dbfs] SQL error: BREAKING MAIN LOOP LIEK NAO");

    event_base_loopbreak(ctx->ev_base);
}

struct dbfs *dbfs_new (struct event_base *ev_base, struct fuse_args *args, const char *db_conninfo) {
    struct dbfs *ctx = NULL;

    // alloc ctx
    if ((ctx = calloc(1, sizeof(*ctx))) == NULL)
        ERROR("calloc");
    
    ctx->ev_base = ev_base;
    ctx->db_conninfo = db_conninfo;

    // open sql
    if ((ctx->db = evsql_new_pq(ctx->ev_base, ctx->db_conninfo, dbfs_sql_error, ctx)) == NULL)
        ERROR("evsql_new_pq");

    // open fuse
    if ((ctx->ev_fuse = evfuse_new(ctx->ev_base, args, &dbfs_llops, ctx)) == NULL)
        ERROR("evfuse_new");

    // success
    return ctx;

error:
    if (ctx)
        dbfs_free(ctx);

    return NULL;
}    

void dbfs_free (struct dbfs *ctx) {
    // cleanup
    if (ctx->ev_fuse) {
        evfuse_free(ctx->ev_fuse);
    
        ctx->ev_fuse = NULL;
    }

    if (ctx->db) {
        // XXX: not yet implemented 
        // evsql_close(ctx->db);
        // ctx->db = NULL;
    }
    
    free(ctx);
}
