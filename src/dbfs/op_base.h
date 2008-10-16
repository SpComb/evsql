#ifndef DBFS_OP_BASE_H
#define DBFS_OP_BASE_H

#include "dbfs.h"

// forward-declaration for callbacks
struct dbfs_op;

/*
 * Called by dbfs_op_free to release any resources when the op is free'd (i.e. not open anymore).
 */
typedef void (*dbfs_op_free_cb) (struct dbfs_op *op_base);

/*
 * Called after the transaction has been opened, and before reply_open.
 *
 * You can do any at-open initialization here.
 */
typedef void (*dbfs_op_open_cb) (struct dbfs_op *op_base);

// the base op state
struct dbfs_op {
    struct fuse_file_info fi;
    struct fuse_req *req;

    struct evsql_trans *trans;
    
    // op target inode
    uint32_t ino;
    
    // open has returned and release hasn't been called yet
    int open;

    // callbacks
    dbfs_op_free_cb free_fn;
    dbfs_op_open_cb open_fn;
};

/*
 * This will handle failures during requests.
 *
 * 1) if we have a trans, abort it
 * 2) fail the req (mandatory) with the given err
 *
 * If the op is open, then we don't release it, but if it's not open, then the op will be free'd completely.
 *
 */
void dbfs_op_fail (struct dbfs_op *op, int err);

/*
 * Open the op, that is, store all the initial state, and open a new transaction.
 *
 * The op must be pre-allocated and zero-initialized.
 *
 * This will always set op->req, so op is safe for dbfs_op_fail after this.
 *
 * This does not fail the dirop, handle error replies yourself.
 *
 * Returns zero on success, err on failure.
 */
int dbfs_op_open (struct dbfs *ctx, struct dbfs_op *op, struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi, dbfs_op_free_cb free_fn, dbfs_op_open_cb ready_fn);

/*
 * Should be called from open_fn to send the fuse_reply_open with fi and mark the op as open.
 *
 * If the op has failed earlier or fuse_reply_open fails, this will return nonzero. Fail the op yourself.
 */ 
int dbfs_op_open_reply (struct dbfs_op *op);

/*
 * Start handling a normal op requests.
 *
 * Lookup the op for the given fi, validate params, and assign the new req.
 *
 * In case the op failed previously, this will error the req and return NULL, indicating that the req has been handled.
 *
 * Repeat, if this returns NULL, consider req invalid.
 */
struct dbfs_op *dbfs_op_req (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi);

/*
 * Done handling a request, adjust state accordingly.
 *
 * req *must* have been replied to.
 */
int dbfs_op_req_done (struct dbfs_op *op);

/*
 * Handle the op release.
 *
 * This will take care of committing the transaction, sending any reply/error, closing the op and freeing it.
 */
void dbfs_op_release (struct fuse_req *req, fuse_ino_t, struct fuse_file_info *fi);


#endif /* DBFS_OP_BASE_H */
