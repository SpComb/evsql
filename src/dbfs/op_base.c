#include <stdlib.h>
#include <assert.h>

#include "op_base.h"
#include "../lib/log.h"

/*
 * Free the op.
 *
 * The op must any oustanding request responded to first, must not be open, and must not have a transaction.
 *
 * The op will be free'd.
 */
static void dbfs_op_free (struct dbfs_op *op) {
    assert(op);
    assert(!op->open);
    assert(!op->req);
    assert(!op->trans);

    // free_fn
    if (op->free_fn)
        op->free_fn(op);
    
    // and then free the op
    free(op);
}

void dbfs_op_fail (struct dbfs_op *op, int err) {
    assert(op->req);
    
    if (op->trans) {
        // abort the trans
        evsql_trans_abort(op->trans);
        
        op->trans = NULL;
    }

    // send an error reply
    if ((err = fuse_reply_err(op->req, err)))
        // XXX: handle these failures /somehow/, or requests will hang and interrupts might handle invalid ops
        EFATAL(err, "\tdbfs_op.fail %p:%p -> reply with fuse_reply_err", op, op->req);
   
    // drop the req
    op->req = NULL;

    // is it open?
    if (!op->open) {
        // no, we can free it now and then forget about the whole thing
        dbfs_op_free(op);

    } else {
        // we need to wait for release

    }
}

/*
 * The op_open transaction is ready for use.
 */
static void dbfs_op_ready (struct evsql_trans *trans, void *arg) {
    struct dbfs_op *op = arg;
    
    assert(trans == op->trans);
    assert(op->req);
    assert(!op->open);

    INFO("\tdbfs_op.ready %p:%p -> trans=%p", op, op->req, trans);

    // remember the transaction
    op->trans = trans;

    // ready
    op->open_fn(op);
    
    // good
    return;
}

/*
 * The op trans was committed, i.e. release has completed
 */
static void dbfs_op_done (struct evsql_trans *trans, void *arg) {
    struct dbfs_op *op = arg;
    int err;
    
    assert(trans == op->trans);
    assert(op->req);
    assert(!op->open);   // should not be considered as open anymore at this point, as errors should release

    INFO("\tdbfs_op.done %p:%p -> OK", op, op->req);

    // forget trans
    op->trans = NULL;
    
    // just reply
    if ((err = fuse_reply_err(op->req, 0)))
        // XXX: handle these failures /somehow/, or requests will hang and interrupts might handle invalid ops
        EFATAL(err, "dbfs_op.done %p:%p -> reply with fuse_reply_err", op, op->req);
    
    // req is done
    op->req = NULL;

    // then we can just free op
    dbfs_op_free(op);
}

/*
 * The op trans has failed, somehow, at some point, some where.
 *
 * This might happend during the open evsql_trans, during a read evsql_query, during the release
 * evsql_trans_commit, or at any point in between.
 *
 * 1) loose the transaction
 * 2) if op has a req, we handle failing it
 */
static void dbfs_op_error (struct evsql_trans *trans, void *arg) {
    struct dbfs_op *op = arg;
    
    // unless we fail 
    assert(trans == op->trans);

    INFO("\tdbfs_op.error %p:%p -> evsql transaction error: %s", op, op->req, evsql_trans_error(trans));
    
    // deassociate the trans
    op->trans = NULL;
    
    // if we were answering a req, error it out, and if the op isn't open, free
    // if we didn't have a req outstanding, the op must be open, so we wouldn't free it in any case, and must wait
    // for the next read/release to detect this and return an error reply
    if (op->req)
        dbfs_op_fail(op, EIO);
    else
        assert(op->open);
}

int dbfs_op_open (struct dbfs *ctx, struct dbfs_op *op, struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi, dbfs_op_free_cb free_fn, dbfs_op_open_cb open_fn) {
    int err;

    assert(op && req && ino && fi);
    assert(!(op->req || op->ino));

    // initialize the op
    op->req = req;
    op->ino = ino;
    // copy *fi since it's on the stack
    op->fi = *fi;
    op->fi.fh = (uint64_t) op;
    op->free_fn = free_fn;
    op->open_fn = open_fn;

    // start a new transaction
    if ((op->trans = evsql_trans(ctx->db, EVSQL_TRANS_SERIALIZABLE, dbfs_op_error, dbfs_op_ready, dbfs_op_done, op)) == NULL)
        SERROR(err = EIO);
    
    // XXX: handle interrupts
    
    // good
    return 0;

error:
    // nothing of ours to cleanup
    return err;
}

int dbfs_op_open_reply (struct dbfs_op *op) {
    int err;
    
    // detect earlier failures
    if (!op->trans && (err = EIO))
        ERROR("op trans has failed");

    // send the openddir reply
    if ((err = fuse_reply_open(op->req, &op->fi)))
        EERROR(err, "fuse_reply_open");
    
    // req is done
    op->req = NULL;

    // op is now open
    op->open = 1;
 
    // good
    return 0;

error:
    return err;
}

struct dbfs_op *dbfs_op_req (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi) {
    struct dbfs_op *op = (struct dbfs_op *) fi->fh;
    int err;
    
    // validate
    assert(op);
    assert(!op->req);
    assert(op->open);
    assert(op->ino == ino);

    // store the new req
    op->req = req;
    
    // detect earlier failures
    if (!op->trans && (err = EIO))
        ERROR("op trans has failed");

    // good
    return op;

error:
    dbfs_op_fail(op, err);
    
    return NULL;
}

int dbfs_op_req_done (struct dbfs_op *op) {
    // validate
    assert(op);
    assert(op->req);
    assert(op->open);

    // unassign the req
    op->req = NULL;

    // k
    return 0;
}

void dbfs_op_release (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi) {
    struct dbfs_op *op = (struct dbfs_op *) fi->fh;
    int err;
    
    assert(op);
    assert(!op->req);
    assert(op->ino == ino);
    
    // update to this req
    op->req = req;

    // fi is irrelevant, we don't touch the flags anyways
    (void) fi;

    // handle failed trans
    if (!op->trans && (err = EIO))
        ERROR("trans has failed");
    
    // log
    INFO("\tdbfs_op.release %p:%p : ino=%lu, fi=%p : trans=%p", op, req, ino, fi, op->trans);
    
    // we must commit the transaction.
    // Note that this might cause dbfs_op_error to be called, we can tell if that happaned by looking at op->req
    // or op->trans - this means that we need to keep the op open when calling trans_commit, so that op_error
    // doesn't free it out from underneath us.
    if (evsql_trans_commit(op->trans))
        SERROR(err = EIO);

    // fall-through to cleanup
    err = 0;

error:
    // the op is not open anymore and can be free'd next, because we either:
    // a) already caught an error
    // b) we get+send an error later on
    // c) we get+send the done/no-error later on
    op->open = 0;

    // did the commit/pre-commit-checks fail?
    if (err) {
        // a) the trans failed earlier (read), so we have a req but no trans
        // b) the trans commit failed, op_error got called -> no req and no trans
        // c) the trans commit failed, op_error did not get called -> have req and trans
        // we either have a req (may or may not have trans), or we don't have a trans either
        // i.e. there is no situation where we don't have a req but do have a trans

        if (op->req)
            dbfs_op_fail(op, err);
        else
            assert(!op->trans);

    } else {
        // shouldn't slip by, op_done should not get called directly. Once it does, it will handle both.
        assert(op->req);
        assert(op->trans);
    }
}

