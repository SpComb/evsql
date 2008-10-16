#ifndef DBFS_H
#define DBFS_H

#include "evfuse.h"

/*
 * External interface for dbfs
 */

/*
 * Context struct.
 */
struct dbfs;

/*
 * Create the evsql and evfuse contexts and run the fs
 */
struct dbfs *dbfs_new (struct event_base *ev_base, struct fuse_args *args, const char *db_conninfo);

/*
 * Release the dbfs's resources and free it
 */
void dbfs_free (struct dbfs *ctx);

#endif /* DBFS_H */
