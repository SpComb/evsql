#ifndef DBFS_DBFS_H
#define DBFS_DBFS_H

#include <sys/stat.h>
#include <errno.h>

#include <event2/event.h>

#include "ops.h"
#include "../evfuse.h"
#include "../evsql.h"
#include "../lib/error.h"

/*
 * Structs and functions shared between all dbfs components
 */

struct dbfs {
    struct event_base *ev_base;
    
    const char *db_conninfo;
    struct evsql *db;

    struct evfuse *ev_fuse;
};

// XXX: not sure how this should work
#define CACHE_TIMEOUT 1.0

// columns used for stat_info
#define DBFS_STAT_COLS " inodes.type, inodes.mode, dbfs_size(inodes.type, inodes.data, inodes.link_path), (SELECT COUNT(*) FROM inodes i LEFT JOIN file_tree ft ON (i.ino = ft.ino) WHERE i.ino = inodes.ino) AS nlink"

/*
 * Convert the CHAR(4) inodes.type from SQL into a mode_t.
 *
 * Returns zero for unknown types.
 */
mode_t _dbfs_mode (const char *type);

/*
 * Check that the number of rows and columns in the result set matches what we expect.
 *
 * If rows is nonzero, there must be exactly that many rows (mostly useful for rows=1).
 * The number of columns must always be given, and match.
 *
 * Returns;
 *  -1  if the query failed, the columns/rows do not match
 *  0   the results match
 *  1   there were no results (zero rows)
 */
int _dbfs_check_res (const struct evsql_result_info *res, size_t rows, size_t cols);

/*
 * Same as _dbfs_check_res, but returns ENOENT/EIO directly
 */
err_t dbfs_check_result (const struct evsql_result_info *res, size_t rows, size_t cols);

/*
 * Fill a `struct state` with info retrieved from a SQL query.
 *
 * The result must contain four columns, starting at the given offset:
 *  inodes.type, inodes.mode, inodes.size, count(*) AS nlink
 *
 * Note that this does not fill the st_ino field
 */
int _dbfs_stat_info (struct stat *st, const struct evsql_result_info *res, size_t row, size_t col_offset);

/** interrupt.c 
 *  
 * Fuse interrupts are handled using fuse_req_interrupt_func. Calling this registers a callback function with the req,
 * which may or may not be called either by fuse_req_interrupt_func, or later on via evfuse's event handler. It is
 * assumed that this will never be called after a call to fuse_reply_*.
 *
 * Hence, to handle an interrupt, we must first ensure that fuse_reply_* will not be called afterwards (it'll return
 * an error), and then we must call fuse_reply_err(req, EINTR).
 *
 * In the simplest case, we can simply submit a query, and then abort it once the req is interrupted (now or later).
 * In the more complicated case, we can check if the request was interrupted, if not, do the query and handle
 * interrupts.
 */

/*
 * Useable as a callback to fuse_req_interrupt_func, will abort the given query and err the req.
 *
 * Due to a locking bug in libfuse 2.7.4, this will actually delay the fuse_req_err until the next event-loop iteration.
 */
void dbfs_interrupt_query (struct fuse_req *req, void *query_ptr);

/*
 * XXX: More complicated state, is this actually needed?
 */
struct dbfs_interrupt_ctx {
    struct fuse_req *req;
    struct evsql_query *query;

    int interrupted : 1;
};

/*
 * Register as a fuse interrupt function for simple requests that only run one query without allocating any resources.
 *
 * This will abort the query if the interrupt is run, causing it's callback to not be called.
 *
 * Returns nonzero if the request was already interrupted, zero otherwise. Be careful that the interrupt does not get
 * fired between you checking for it and setting query.
 */
int dbfs_interrupt_register (struct fuse_req *req, struct dbfs_interrupt_ctx *ctx);

#endif /* DBFS_DBFS_H */
