
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

/*
 * Check the result set.
 *
 * Returns;
 *  -1  if the query failed, the columns do not match, or there are too many/few rows
 *  0   the results match
 *  1   there were no results
 */
int _dbfs_check_res (const struct evsql_result_info *res, size_t rows, size_t cols) {
    int err = 0;

    // check if it failed
    if (res->error)
        NERROR(evsql_result_error(res));
        
    // not found?
    if (evsql_result_rows(res) == 0)
        SERROR(err = 1);

    // duplicate rows?
    if (evsql_result_rows(res) != rows)
        ERROR("multiple rows returned");
    
    // correct number of columns
    if (evsql_result_cols(res) != 5)
        ERROR("wrong number of columns: %zu", evsql_result_cols(res));

    // good
    return 0;

error:
    if (!err)
        err = -1;

    return err;
}

int _dbfs_stat_info (struct stat *st, const struct evsql_result_info *res, size_t row, size_t col_offset) {
    int err = 0;
    
    uint16_t mode;
    uint64_t size, nlink;
    const char *type;
    
    // extract the data
    if (0
        ||  evsql_result_string(res, row, col_offset + 0, &type,       0 ) // inodes.type
        ||  evsql_result_uint16(res, row, col_offset + 1, &mode,       0 ) // inodes.mode
        ||  evsql_result_uint64(res, row, col_offset + 2, &size,       0 ) // inodes.size
        ||  evsql_result_uint64(res, row, col_offset + 3, &nlink,      0 ) // count(*)
    )
        EERROR(err = EIO, "invalid db data");

    INFO("\tst_mode=S_IF%s | %ho, st_nlink=%llu, st_size=%llu", type, mode, (long long unsigned int) nlink, (long long unsigned int) size);

    // convert and store
    st->st_mode = _dbfs_mode(type) | mode;
    st->st_nlink = nlink;
    st->st_size = size;
    
    // good
    return 0;

error:
    return -1;
}

void _dbfs_lookup_result (const struct evsql_result_info *res, void *arg) {
    struct fuse_req *req = arg;
    struct fuse_entry_param e; ZINIT(e);
    int err = 0;
    
    uint32_t ino;
    
    // check the results
    if ((err = _dbfs_check_res(res, 1, 5)))
        SERROR(err = (err ==  1 ? ENOENT : EIO));
    
    // get the data
    if (0
        ||  evsql_result_uint32(res, 0, 0, &ino,        0 ) // inodes.ino
    )
        EERROR(err = EIO, "invalid db data");
        
    INFO("[dbfs.lookup] -> ion=%u", ino);
    
    // stat attrs
    if (_dbfs_stat_info(&e.attr, res, 0, 1))
        goto error;

    // other attrs
    e.ino = e.attr.st_ino = ino;
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
        " WHERE file_tree.parent = $1::int4 AND file_tree.name = $2::varchar"
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
    if (evsql_query_params(ctx->db, NULL, sql, &params, _dbfs_lookup_result, req) == NULL)
        EERROR(err = EIO, "evsql_query_params");

    // XXX: handle interrupts
    
    // wait
    return;

error:
    if ((err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");
}

void _dbfs_getattr_result (const struct evsql_result_info *res, void *arg) {
    struct fuse_req *req = arg;
    struct stat st; ZINIT(st);
    int err = 0;
    
    // check the results
    if ((err = _dbfs_check_res(res, 1, 4)))
        SERROR(err = (err ==  1 ? ENOENT : EIO));
        
    INFO("[dbfs.getattr %p] -> (stat follows)", req);
    
    // stat attrs
    if (_dbfs_stat_info(&st, res, 0, 0))
        goto error;

    // XXX: we don't have the ino
    st.st_ino = 0;

    // reply
    if ((err = fuse_reply_attr(req, &st, CACHE_TIMEOUT)))
        EERROR(err, "fuse_reply_entry");

error:
    if (err && (err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");

    // free
    evsql_result_free(res);
}

static void dbfs_getattr (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi) {
    struct dbfs *ctx = fuse_req_userdata(req);
    int err;
    
    (void) fi;

    INFO("[dbfs.getattr %p] ino=%lu", req, ino);

    const char *sql =
        "SELECT"
        " inodes.type, inodes.mode, inodes.size, count(*)"
        " FROM inodes"
        " WHERE inodes.ino = ‰1::int4"
        " GROUP BY inodes.type, inodes.mode, inodes.size";

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
    if (evsql_query_params(ctx->db, NULL, sql, &params, _dbfs_getattr_result, req) == NULL)
        SERROR(err = EIO);

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

    .getattr        = dbfs_getattr,
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

