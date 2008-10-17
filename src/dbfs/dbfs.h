#ifndef DBFS_DBFS_H
#define DBFS_DBFS_H

#include <sys/stat.h>
#include <errno.h>

#include <event2/event.h>

#include "ops.h"
#include "../evfuse.h"
#include "../evsql.h"

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
 * Fill a `struct state` with info retrieved from a SQL query.
 *
 * The result must contain four columns, starting at the given offset:
 *  inodes.type, inodes.mode, inodes.size, count(*) AS nlink
 *
 * Note that this does not fill the st_ino field
 */
int _dbfs_stat_info (struct stat *st, const struct evsql_result_info *res, size_t row, size_t col_offset);

#endif /* DBFS_DBFS_H */
