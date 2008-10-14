
#include <stdlib.h>
#include <assert.h>

#include "common.h"
#include "ops.h"
#include "../dirbuf.h"

/*
 * Directory related functionality like opendir, readdir, releasedir
 */

struct dbfs_dirop {
    struct fuse_file_info fi;
    struct fuse_req *req;

    struct evsql_trans *trans;
    
    // dir/parent dir inodes
    uint32_t ino, parent;
    
    // opendir has returned and releasedir hasn't been called yet
    int open;

    // for readdir
    struct dirbuf dirbuf;
};

/*
 * Free the dirop, aborting any in-progress transaction.
 *
 * The dirop must any oustanding request responded to first, must not be open, and must not have a transaction.
 *
 * The dirbuf will be released, and the dirop free'd.
 */
static void _dbfs_dirop_free (struct dbfs_dirop *dirop) {
    assert(dirop);
    assert(!dirop->open);
    assert(!dirop->req);
    assert(!dirop->trans);
    
    // just release the dirbuf
    dirbuf_release(&dirop->dirbuf);
    
    // and then free the dirop
    free(dirop);
}

/*
 * This will handle backend failures during requests.
 *
 * 1) if we have a trans, abort it
 * 2) fail the req (mandatory)
 *
 * If the dirop is open, then we don't release it, but if it's not open, then the dirop will be free'd completely.
 *
 */
static void _dbfs_dirop_fail (struct dbfs_dirop *dirop) {
    int err;

    assert(dirop->req);
    
    if (dirop->trans) {
        // abort the trans
        evsql_trans_abort(dirop->trans);
        
        dirop->trans = NULL;
    }

    // send an error reply
    if ((err = fuse_reply_err(dirop->req, err)))
        // XXX: handle these failures /somehow/, or requests will hang and interrupts might handle invalid dirops
        EFATAL(err, "dbfs.fail %p:%p dirop_fail: reply with fuse_reply_err", dirop, dirop->req);
   
    // drop the req
    dirop->req = NULL;

    // is it open?
    if (!dirop->open) {
        // no, we can free it now and then forget about the whole thing
        _dbfs_dirop_free(dirop);

    } else {
        // we need to wait for releasedir

    }
}

/*
 * Handle the results for the initial attribute lookup for the dir itself during opendir ops.
 */
static void dbfs_opendir_info_res (const struct evsql_result_info *res, void *arg) {
    struct dbfs_dirop *dirop = arg;
    int err;
    
    assert(dirop->trans);
    assert(dirop->req);
    assert(!dirop->open);
   
    // check the results
    if ((err = _dbfs_check_res(res, 1, 2)))
        SERROR(err = (err ==  1 ? ENOENT : EIO));

    const char *type;

    // extract the data
    if (0
        ||  evsql_result_uint32(res, 0, 0, &dirop->parent,  1 ) // file_tree.parent
        ||  evsql_result_string(res, 0, 1, &type,           0 ) // inodes.type
    )
        SERROR(err = EIO);

    // is it a dir?
    if (_dbfs_mode(type) != S_IFDIR)
        EERROR(err = ENOTDIR, "wrong type: %s", type);
    
    INFO("[dbfs.opendir %p:%p] -> ino=%lu, parent=%lu, type=%s", dirop, dirop->req, (unsigned long int) dirop->ino, (unsigned long int) dirop->parent, type);
    
    // send the openddir reply
    if ((err = fuse_reply_open(dirop->req, &dirop->fi)))
        EERROR(err, "fuse_reply_open");
    
    // req is done
    dirop->req = NULL;

    // dirop is now open
    dirop->open = 1;

    // success, fallthrough for evsql_result_free
    err = 0;

error:
    if (err)
        // fail it
        _dbfs_dirop_fail(dirop);
    
    // free
    evsql_result_free(res);
}

/*
 * The opendir transaction is ready for use. Query for the given dir's info
 */
static void dbfs_dirop_ready (struct evsql_trans *trans, void *arg) {
    struct dbfs_dirop *dirop = arg;
    struct dbfs *ctx = fuse_req_userdata(dirop->req);
    int err;
    
    // XXX: unless we abort queries
    assert(trans == dirop->trans);
    assert(dirop->req);
    assert(!dirop->open);

    INFO("[dbfs.opendir %p:%p] -> trans=%p", dirop, dirop->req, trans);

    // remember the transaction
    dirop->trans = trans;
    
    // first fetch info about the dir itself
    const char *sql =
        "SELECT"
        " file_tree.parent, inodes.type"
        " FROM file_tree LEFT OUTER JOIN inodes ON (file_tree.inode = inodes.ino)"
        " WHERE file_tree.inode = $1::int4";

    static struct evsql_query_params params = EVSQL_PARAMS(EVSQL_FMT_BINARY) {
        EVSQL_PARAM ( UINT32 ),

        EVSQL_PARAMS_END
    };

    // build params
    if (0
        ||  evsql_param_uint32(&params, 0, dirop->ino)
    )
        SERROR(err = EIO);
        
    // query
    if (evsql_query_params(ctx->db, dirop->trans, sql, &params, dbfs_opendir_info_res, dirop) == NULL)
        SERROR(err = EIO);

    // ok, wait for the info results
    return;

error:
    // fail it
    _dbfs_dirop_fail(dirop);
}

/*
 * The dirop trans was committed, i.e. releasedir has completed
 */
static void dbfs_dirop_done (struct evsql_trans *trans, void *arg) {
    struct dbfs_dirop *dirop = arg;
    int err;
    
    assert(dirop->trans);
    assert(dirop->req);
    assert(!dirop->open);   // should not be considered as open anymore at this point, as errors should release

    INFO("[dbfs.releasedir %p:%p] -> OK", dirop, dirop->req);

    // forget trans
    dirop->trans = NULL;
    
    // just reply
    if ((err = fuse_reply_err(dirop->req, 0)))
        // XXX: handle these failures /somehow/, or requests will hang and interrupts might handle invalid dirops
        EFATAL(err, "[dbfs.releasedir %p:%p] dirop_done: reply with fuse_reply_err", dirop, dirop->req);
    
    // req is done
    dirop->req = NULL;

    // then we can just free dirop
    _dbfs_dirop_free(dirop);
}

/*
 * The dirop trans has failed, somehow, at some point, some where.
 *
 * This might happend during the opendir evsql_trans, during a readdir evsql_query, during the releasedir
 * evsql_trans_commit, or at any point in between.
 *
 * 1) loose the transaction
 * 2) if dirop has a req, we handle failing it
 */
static void dbfs_dirop_error (struct evsql_trans *trans, void *arg) {
    struct dbfs_dirop *dirop = arg;

    INFO("[dbfs:dirop %p:%p] evsql transaction error: %s", dirop, dirop->req, evsql_trans_error(trans));
    
    // deassociate the trans
    dirop->trans = NULL;
    
    // if we were answering a req, error it out, and if the dirop isn't open, release it
    // if we didn't have a req outstanding, the dirop must be open, so we wouldn't free it in any case, and must wait
    // for the next readdir/releasedir to detect this and return an error reply
    if (dirop->req)
        _dbfs_dirop_fail(dirop);
    else
        assert(dirop->open);
}

/*
 * Handle opendir(), this means starting a new transaction, dbfs_dirop_ready/error will continue on from there.
 *
 * The contents of fi will be copied into the dirop, and will be used as the basis for the fuse_reply_open reply.
 */
void dbfs_opendir (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi) {
    struct dbfs *ctx = fuse_req_userdata(req);
    struct dbfs_dirop *dirop = NULL;
    int err;
    
    // allocate it
    if ((dirop = calloc(1, sizeof(*dirop))) == NULL && (err = EIO))
        ERROR("calloc");

    INFO("[dbfs.opendir %p:%p] ino=%lu, fi=%p", dirop, req, ino, fi);
    
    // store the dirop
    // copy *fi since it's on the stack
    dirop->fi = *fi;
    dirop->fi.fh = (uint64_t) dirop;
    dirop->req = req;
    dirop->ino = ino;

    // start a new transaction
    if ((dirop->trans = evsql_trans(ctx->db, EVSQL_TRANS_SERIALIZABLE, dbfs_dirop_error, dbfs_dirop_ready, dbfs_dirop_done, dirop)) == NULL)
        SERROR(err = EIO);
    
    // XXX: handle interrupts
    
    // wait
    return;

error:
    if (dirop) {
        // we can fail normally
        _dbfs_dirop_fail(dirop);

    } else {
        // must error out manually as we couldn't alloc the context
        if ((err = fuse_reply_err(req, err)))
            EWARNING(err, "fuse_reply_err");
    }
}

/*
 * Got the list of files for our readdir() request.
 *
 * Fill up the dirbuf, and then send the reply.
 *
 */
static void dbfs_readdir_files_res (const struct evsql_result_info *res, void *arg) {
    struct dbfs_dirop *dirop = arg;
    int err;
    size_t row;
    
    assert(dirop->req);
    assert(dirop->trans);
    assert(dirop->open);
    
    // check the results
    if ((err = _dbfs_check_res(res, 0, 4)) < 0)
        SERROR(err = EIO);
        
    INFO("[dbfs.readdir %p:%p] -> files: res_rows=%zu", dirop, dirop->req, evsql_result_rows(res));
        
    // iterate over the rows
    for (row = 0; row < evsql_result_rows(res); row++) {
        uint32_t off, ino;
        const char *name, *type;

        // extract the data
        if (0
            ||  evsql_result_uint32(res, row, 0, &off,          0 ) // file_tree.offset
            ||  evsql_result_string(res, row, 1, &name,         0 ) // file_tree.name
            ||  evsql_result_uint32(res, row, 2, &ino,          0 ) // inodes.ino
            ||  evsql_result_string(res, row, 3, &type,         0 ) // inodes.type
        )
            SERROR(err = EIO);
        
        INFO("\t%zu: off=%lu+2, name=%s, ino=%lu, type=%s", row, (long unsigned int) off, name, (long unsigned int) ino, type);

        // add to the dirbuf
        // offsets are just offset + 2
        if ((err = dirbuf_add(dirop->req, &dirop->dirbuf, off + 2, off + 3, name, ino, _dbfs_mode(type))) < 0 && (err = EIO))
            ERROR("failed to add dirent for inode=%lu", (long unsigned int) ino);
        
        // stop if it's full
        if (err > 0)
            break;
    }

    // send it
    if ((err = dirbuf_done(dirop->req, &dirop->dirbuf)))
        EERROR(err, "failed to send buf");

    // req is done
    dirop->req = NULL;
    
    // good, fallthrough
    err = 0;

error:
    if (err)
        _dbfs_dirop_fail(dirop);

    // free
    evsql_result_free(res);
}

/*
 * Handle a readdir request. This will execute a SQL query inside the transaction to get the files at the given offset,
 * and _dbfs_readdir_res will handle the results.
 *
 * If trans failed earlier, detect that and return an error.
 */
void dbfs_readdir (struct fuse_req *req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi) {
    struct dbfs *ctx = fuse_req_userdata(req);
    struct dbfs_dirop *dirop = (struct dbfs_dirop *) fi->fh;
    int err;
    
    assert(dirop);
    assert(!dirop->req);
    assert(dirop->open);
    assert(dirop->ino == ino);
    
    // store the new req
    dirop->req = req;

    // detect earlier failures
    if (!dirop->trans && (err = EIO))
        ERROR("dirop trans has failed");
    
    INFO("[dbfs.readdir %p:%p] ino=%lu, size=%zu, off=%zu, fi=%p : trans=%p", dirop, req, ino, size, off, fi, dirop->trans);

    // create the dirbuf
    if (dirbuf_init(&dirop->dirbuf, size, off))
        SERROR(err = EIO);

    // add . and ..
    // we set the next offset to 2, because all dirent offsets will be larger than that
    // assume that these two should *always* fit
    if ((err = (0
        ||  dirbuf_add(req, &dirop->dirbuf, 0, 1, ".",   dirop->ino,    S_IFDIR )
        ||  dirbuf_add(req, &dirop->dirbuf, 1, 2, "..",  
                        dirop->parent ? dirop->parent : dirop->ino,     S_IFDIR )
    )) && (err = EIO))
        ERROR("failed to add . and .. dirents");

    // select all relevant file entries
    const char *sql = 
        "SELECT"
        " file_tree.\"offset\", file_tree.name, inodes.ino, inodes.type"
        " FROM file_tree LEFT OUTER JOIN inodes ON (file_tree.inode = inodes.ino)"
        " WHERE file_tree.parent = $1::int4 AND file_tree.\"offset\" >= $2::int4"
        " LIMIT $3::int4";

    static struct evsql_query_params params = EVSQL_PARAMS(EVSQL_FMT_BINARY) {
        EVSQL_PARAM ( UINT32 ),
        EVSQL_PARAM ( UINT32 ),
        EVSQL_PARAM ( UINT32 ),

        EVSQL_PARAMS_END
    };

    // adjust offset to take . and .. into account
    if (off > 2)
        off -= 2;
    
    // build params
    if (0
        ||  evsql_param_uint32(&params, 0, dirop->ino)
        ||  evsql_param_uint32(&params, 1, off)
        ||  evsql_param_uint32(&params, 2, dirbuf_estimate(&dirop->dirbuf, 0))
    )
        SERROR(err = EIO);

    // query
    if (evsql_query_params(ctx->db, dirop->trans, sql, &params, dbfs_readdir_files_res, dirop) == NULL)
        SERROR(err = EIO);

    // good, wait
    return;

error:
    _dbfs_dirop_fail(dirop);
}

/*
 * "For every [succesfull] opendir call there will be exactly one releasedir call."
 *
 * The dirop may be in a failed state.
 */
void dbfs_releasedir (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi) {
    struct dbfs *ctx = fuse_req_userdata(req);
    struct dbfs_dirop *dirop = (struct dbfs_dirop *) fi->fh;
    int err;

    (void) ctx;
    
    assert(dirop);
    assert(!dirop->req);
    assert(dirop->ino == ino);
    
    // update to this req
    dirop->req = req;

    // fi is irrelevant, we don't touch the flags anyways
    (void) fi;

    // handle failed trans
    if (!dirop->trans)
        ERROR("trans has failed");
    
    // log
    INFO("[dbfs.releasedir %p:%p] ino=%lu, fi=%p : trans=%p", dirop, req, ino, fi, dirop->trans);
    
    // we must commit the transaction (although it was jut SELECTs, no changes).
    // Note that this might cause dbfs_dirop_error to be called, we can tell if that happaned by looking at dirop->req
    // or dirop->trans this means that we need to keep the dirop open when calling trans_commit, so that dirop_error
    // doesn't free it out from underneath us.
    if (evsql_trans_commit(dirop->trans))
        SERROR(err = EIO);

    // fall-through to cleanup
    err = 0;

error:
    // the dirop is not open anymore and can be free'd:
    // a) if we already caught an error
    // b) if we get+send an error later on
    // c) if we get+send the done/no-error later on
    dirop->open = 0;

    // did the commit/pre-commit-checks fail?
    if (err) {
        // a) the trans failed earlier (readdir), so we have a req but no trans
        // b) the trans commit failed, dirop_error got called -> no req and no trans
        // c) the trans commit failed, dirop_error did not get called -> have req and trans
        // we either have a req (may or may not have trans), or we don't have a trans either
        // i.e. there is no situation where we don't have a req but do have a trans

        if (dirop->req)
            _dbfs_dirop_fail(dirop);
        else
            assert(!dirop->trans);

    } else {
        // shouldn't slip by, dirop_done should not get called directly. Once it does, it will handle both.
        assert(dirop->req);
        assert(dirop->trans);
    }
}

