
/*
 * A simple PostgreSQL-based filesystem.
 */

#include <string.h>
#include <errno.h>

#include <event2/event.h>

#include "evfuse.h"
#include "evsql.h"
#include "dirbuf.h"
#include "lib/log.h"
#include "lib/signals.h"
#include "lib/misc.h"

#define SERROR(val) do { (val); goto error; } while(0)

struct dbfs {
    struct event_base *ev_base;
    struct signals *signals;
    
    const char *db_conninfo;
    struct evsql *db;

    struct evfuse *ev_fuse;
};

#define CONNINFO_DEFAULT "dbname=test"

// XXX: not sure how this should work
#define CACHE_TIMEOUT 1.0

mode_t _dbfs_mode (const char *type) {
    if (!strcmp(type, "DIR"))
        return S_IFDIR;

    if (!strcmp(type, "REG"))
        return S_IFREG;

    else {
        WARNING("[dbfs] weird mode-type: %s", type);
        return 0;
    }
}

void dbfs_init (void *userdata, struct fuse_conn_info *conn) {
    INFO("[dbfs.init] userdata=%p, conn=%p", userdata, conn);

}

void dbfs_destroy (void *userdata) {
    INFO("[dbfs.destroy] userdata=%p", userdata);


}

void _dbfs_lookup_result (const struct evsql_result_info *res, void *arg) {
    struct fuse_req *req = arg;
    struct fuse_entry_param e; ZINIT(e);
    int err = 0;
    
    uint16_t mode;
    uint32_t ino;
    uint64_t size, nlink;
    const char *type;
    
    // check if it failed
    if (res->error && (err = EIO))
        NERROR(evsql_result_error(res));

    // duplicate rows?
    if (evsql_result_rows(res) > 1)
        EERROR(err = EIO, "multiple rows returned");

    // not found?
    if (evsql_result_rows(res) == 0)
        SERROR(err = ENOENT);
    
    // correct number of columns
    if (evsql_result_cols(res) != 5)
        EERROR(err = EIO, "wrong number of columns: %zu", evsql_result_cols(res));
    
    // get the data
    if (0
        ||  evsql_result_uint32(res, 0, 0, &ino,        0 ) // inodes.ino
        ||  evsql_result_string(res, 0, 1, &type,       0 ) // inodes.type
        ||  evsql_result_uint16(res, 0, 2, &mode,       0 ) // inodes.mode
        ||  evsql_result_uint64(res, 0, 3, &size,       0 ) // inodes.size
        ||  evsql_result_uint64(res, 0, 4, &nlink,      0 ) // count(*)
    )
        EERROR(err = EIO, "invalid db data");
    
    INFO("[dbfs.look] -> ino=%u, st_mode=S_IF%s | %ho, st_nlink=%llu, st_size=%llu", ino, type, mode, (long long unsigned int) nlink, (long long unsigned int) size);

    // convert and store
    e.ino = e.attr.st_ino = ino;
    e.attr.st_mode = _dbfs_mode(type) | mode;
    e.attr.st_nlink = nlink;
    e.attr.st_size = size;
    
    // XXX: timeouts
    e.attr_timeout = CACHE_TIMEOUT;
    e.entry_timeout = CACHE_TIMEOUT;
        
    // reply
    if ((err = fuse_reply_entry(req, &e)))
        EERROR(err, "fuse_reply_entry");

error:
    if (err && (err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");

    // free
    evsql_result_free(res);
}

void dbfs_lookup (struct fuse_req *req, fuse_ino_t parent, const char *name) {
    struct dbfs *ctx = fuse_req_userdata(req);
    int err;

    INFO("[dbfs.lookup] parent=%lu name=%s", parent, name);
    
    // query and params
    const char *sql = 
        "SELECT"
        " inodes.ino, inodes.type, inodes.mode, inodes.size, count(*)"
        " FROM file_tree INNER JOIN inodes ON (file_tree.inode = inodes.ino)"
        " WHERE file_tree.parent = $1::int AND file_tree.name = $2::varchar"
        " GROUP BY inodes.ino, inodes.type, inodes.mode, inodes.size";
    
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
    if (evsql_query_params(ctx->db, sql, &params, _dbfs_lookup_result, req) == NULL)
        EERROR(err = EIO, "evsql_query_params");

    // XXX: handle interrupts
    
    // wait
    return;

error:
    if ((err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");
}

struct fuse_lowlevel_ops dbfs_llops = {
    .init           = dbfs_init,
    .destroy        = dbfs_destroy,
    
    .lookup         = dbfs_lookup,
};

void dbfs_sql_error (struct evsql *evsql, void *arg) {
    struct dbfs *ctx = arg;

    // AAAAAAAAAA.... panic
    WARNING("[dbfs] SQL error: BREAKING MAIN LOOP LIEK NAO");

    event_base_loopbreak(ctx->ev_base);
}

int main (int argc, char **argv) {
    struct fuse_args fuse_args = FUSE_ARGS_INIT(argc, argv);
    struct dbfs ctx; ZINIT(ctx);
    
    // parse args, XXX: fuse_args
    ctx.db_conninfo = CONNINFO_DEFAULT;
    
    // init libevent
    if ((ctx.ev_base = event_base_new()) == NULL)
        ERROR("event_base_new");
    
    // setup signals
    if ((ctx.signals = signals_default(ctx.ev_base)) == NULL)
        ERROR("signals_default");

    // open sql
    if ((ctx.db = evsql_new_pq(ctx.ev_base, ctx.db_conninfo, dbfs_sql_error, &ctx)) == NULL)
        ERROR("evsql_new_pq");

    // open fuse
    if ((ctx.ev_fuse = evfuse_new(ctx.ev_base, &fuse_args, &dbfs_llops, &ctx)) == NULL)
        ERROR("evfuse_new");

    // run libevent
    INFO("running libevent loop");

    if (event_base_dispatch(ctx.ev_base))
        PERROR("event_base_dispatch");
    
    // clean shutdown

error :
    // cleanup
    if (ctx.ev_fuse)
        evfuse_close(ctx.ev_fuse);

    // XXX: ctx.db
    
    if (ctx.signals)
        signals_free(ctx.signals);

    if (ctx.ev_base)
        event_base_free(ctx.ev_base);
    
    fuse_opt_free_args(&fuse_args);
}

