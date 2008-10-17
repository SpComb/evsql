#ifndef DBFS_TRANS_H
#define DBFS_TRANS_H

/*
 * Support for single-fuse_req transactions.
 */

#include "dbfs.h"

// forward-declaration
struct dbfs_trans;

// generic callback
typedef void (*dbfs_trans_cb) (struct dbfs_trans *ctx);

/*
 * Request/transaction state.
 */
struct dbfs_trans {
    struct fuse_req *req;
    struct evsql_trans *trans;
    
    // called when the dbfs_trans is being free'd
    dbfs_trans_cb free_fn;

    // called once the transaction is ready
    dbfs_trans_cb begin_fn;

    // called once the transaction has been commited
    dbfs_trans_cb commit_fn;

    // set by trans_error to EIO if !NULL
    int *err_ptr;
};

/*
 * Call from commit_fn once you've set req to NULL. Also called from trans_fail.
 *
 * Will call free_fn if present.
 */
void dbfs_trans_free (struct dbfs_trans *ctx);

/*
 * Fail the dbfs_trans, aborting any trans, erroring out any req and freeing the ctx.
 */
void dbfs_trans_fail (struct dbfs_trans *ctx, int err);

/*
 * Initialize the ctx with the given req (stays the same during the lifetime), and opens the transaction.
 *
 * You should set the callback functions after/before calling this.
 *
 * begin_fn will be called once the transaction is open, if that fails, the req will be errored for you.
 *
 * If opening the transaction fails, this will return nonzero and not touch req, otherwise zero.
 */
int dbfs_trans_init (struct dbfs_trans *ctx, struct fuse_req *req);

/*
 * Same as init, but allocates/frees-on-error the dbfs_trans for you.
 */
struct dbfs_trans *dbfs_trans_new (struct fuse_req *req);

/*
 * Commit the dbfs_trans. After calling this, the ctx may or may not be valid, and commit_fn may or may not be called.
 */
void dbfs_trans_commit (struct dbfs_trans *ctx);

#endif /* DBFS_TRANS_H */
