#include <stdlib.h>
#include <assert.h>

#include <postgresql/libpq/libpq-fs.h>

#include "dbfs.h"
#include "op_base.h"
#include "../lib/log.h"

/*
 * A file-operation, i.e. a sequence consisting of an OPEN, a multitude of READ/WRITE, followed by zero or more FLUSHes, and finally a single RELEASE.
 *
 * For historical reasons this opens a transaction and keeps it between open/release, but reads/writes now use the oid directly and are transactionless.
 */

struct dbfs_fileop {
    struct dbfs_op base;
    
    uint32_t oid;
//    uint32_t lo_fd;
};

static void _dbfs_fileop_free (struct dbfs_op *op_base) {
    struct dbfs_fileop *fop = (struct dbfs_fileop *) op_base;
    
    /* no-op */
    (void) fop;
}

static void dbfs_open_res (const struct evsql_result_info *res, void *arg) {
    struct dbfs_fileop *fop = arg;
    int err;

    // check the results
    if ((err = _dbfs_check_res(res, 1, 2)))
        SERROR(err = (err ==  1 ? ENOENT : EIO));

    const char *type;

    // extract the data
    if (0
        ||  evsql_result_string(res, 0, 0, &type,           0 ) // inodes.type
        ||  evsql_result_uint32(res, 0, 1, &fop->oid,       0 ) // inodes.data
    )
        SERROR(err = EIO);

    // is it a dir?
    if (_dbfs_mode(type) != S_IFREG)
        EERROR(err = EINVAL, "wrong type: %s", type);
    
    INFO("\t[dbfs.open %p:%p] -> ino=%lu, type=%s", fop, fop->base.req, (unsigned long int) fop->base.ino, type);
    
    // open_fn done, do the open_reply
    if ((err = dbfs_op_open_reply(&fop->base)))
        goto error;

    // success, fallthrough for evsql_result_free
    err = 0;

error:
    if (err)
        // fail it
        dbfs_op_fail(&fop->base, err);
    
    // free
    evsql_result_free(res);
}

static void dbfs_fileop_open (struct dbfs_op *op_base) {
    struct dbfs_fileop *fop = (struct dbfs_fileop *) op_base;
    struct dbfs *ctx = fuse_req_userdata(fop->base.req);
    int err;
    
    // make sure the file actually exists
    const char *sql =
        "SELECT"
        " inodes.type, inodes.data"
        " FROM inodes"
        " WHERE inodes.ino = $1::int4";

    static struct evsql_query_params params = EVSQL_PARAMS(EVSQL_FMT_BINARY) {
        EVSQL_PARAM ( UINT32 ),

        EVSQL_PARAMS_END
    };

    // build params
    if (0
        ||  evsql_param_uint32(&params, 0, fop->base.ino)
    )
        SERROR(err = EIO);
        
    // query
    if (evsql_query_params(ctx->db, fop->base.trans, sql, &params, dbfs_open_res, fop) == NULL)
        SERROR(err = EIO);

    // ok, wait for the info results
    return;

error:
    // fail it
    dbfs_op_fail(&fop->base, err);
}

void dbfs_open (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi) {
    struct dbfs *ctx = fuse_req_userdata(req);
    struct dbfs_fileop *fop = NULL;
    int err;
    
    // allocate it
    if ((fop = calloc(1, sizeof(*fop))) == NULL && (err = EIO))
        ERROR("calloc");

    // do the op_open
    if ((err = dbfs_op_open(ctx, &fop->base, req, ino, fi, _dbfs_fileop_free, dbfs_fileop_open)))
        ERROR("dbfs_op_open");
    
    // log
    INFO("[dbfs.open %p:%p] ino=%lu, fi->flags=%04X", fop, req, ino, fi->flags);
    
    // wait
    return;

error:
    if (fop) {
        // we can fail normally
        dbfs_op_fail(&fop->base, err);

    } else {
        // must error out manually as we couldn't alloc the context
        if ((err = -fuse_reply_err(req, err)))
            EWARNING(err, "fuse_reply_err");
    }
}

void dbfs_read_res (const struct evsql_result_info *res, void *arg) {
    struct fuse_req *req = arg;
    int err;
    const char *buf;
    size_t size;
 
    // check the results
    if ((err = _dbfs_check_res(res, 1, 1)) < 0)
        SERROR(err = EIO);
        
    // get the data
    if (evsql_result_binary(res, 0, 0, &buf, &size, 0))
        SERROR(err = EIO);

    INFO("\t[dbfs.read %p] -> size=%zu", req, size);
        
    // send it
    if ((err = -fuse_reply_buf(req, buf, size)))
        EERROR(err, "fuse_reply_buf");
    
    // good, fallthrough
    err = 0;

error:
    if (err)
        fuse_reply_err(req, err);


    // free
    evsql_result_free(res);
}

void dbfs_read (struct fuse_req *req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi) {
    struct dbfs *ctx = fuse_req_userdata(req);
    int err;
    
    // log
    INFO("[dbfs.read %p] ino=%lu, size=%zu, off=%lu, fi->flags=%04X", req, ino, size, off, fi->flags);

    // query
    const char *sql = 
        "SELECT"
        " lo_pread_oid(data, $1::int4, $2::int4)"
        " FROM inodes"
        " WHERE ino = $3::int4";
    
    static struct evsql_query_params params = EVSQL_PARAMS(EVSQL_FMT_BINARY) {
        EVSQL_PARAM ( UINT32 ), // len
        EVSQL_PARAM ( UINT32 ), // off
        EVSQL_PARAM ( UINT32 ), // ino

        EVSQL_PARAMS_END
    };

    // build params
    if (0
        ||  evsql_param_uint32(&params, 0, size)
        ||  evsql_param_uint32(&params, 1, off)
        ||  evsql_param_uint32(&params, 2, ino)
    )
        SERROR(err = EIO);
        
    // query, transactionless
    if (evsql_query_params(ctx->db, NULL, sql, &params, dbfs_read_res, req) == NULL)
        SERROR(err = EIO);

    // ok, wait for the info results
    return;

error:
    fuse_reply_err(req, err);
}

void dbfs_write_res (const struct evsql_result_info *res, void *arg) {
    struct fuse_req *req = arg;
    int err;
    uint32_t size;
 
    // check the results
    if ((err = _dbfs_check_res(res, 1, 1)) < 0)
        SERROR(err = EIO);
        
    // get the size
    if (evsql_result_uint32(res, 0, 0, &size, 0))
        SERROR(err = EIO);

    INFO("\t[dbfs.write %p] -> size=%lu", req, (long unsigned int) size);
        
    // send it
    if ((err = -fuse_reply_write(req, size)))
        EERROR(err, "fuse_reply_write");

    // good, fallthrough
    err = 0;

error:
    if (err)
        fuse_reply_err(req, err);

    // free
    evsql_result_free(res);
}

void dbfs_write (struct fuse_req *req, fuse_ino_t ino, const char *buf, size_t size, off_t off, struct fuse_file_info *fi) {
    struct dbfs *ctx = fuse_req_userdata(req);
    int err;
    
    // log
    INFO("[dbfs.write %p] ino=%lu, size=%zu, off=%lu, fi->flags=%04X", req, ino, size, off, fi->flags);

    // query
    const char *sql = 
        "SELECT"
        " lo_pwrite_oid(data, $1::bytea, $2::int4)"
        " FROM inodes"
        " WHERE ino = $3::int4";
    
    static struct evsql_query_params params = EVSQL_PARAMS(EVSQL_FMT_BINARY) {
        EVSQL_PARAM ( BINARY ), // buf
        EVSQL_PARAM ( UINT32 ), // off
        EVSQL_PARAM ( UINT32 ), // oid

        EVSQL_PARAMS_END
    };

    // build params
    if (0
        ||  evsql_param_binary(&params, 0, buf, size)
        ||  evsql_param_uint32(&params, 1, off)
        ||  evsql_param_uint32(&params, 2, ino)
    )
        SERROR(err = EIO);
        
    // query
    if (evsql_query_params(ctx->db, NULL, sql, &params, dbfs_write_res, req) == NULL)
        SERROR(err = EIO);

    // ok, wait for the info results
    return;

error:
    fuse_reply_err(req, err);
}

void dbfs_flush (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi) {
    struct dbfs_fileop *fop;
    int err;

    // get the fop
    if ((fop = (struct dbfs_fileop *) dbfs_op_req(req, ino, fi)) == NULL)
        return;
    
    // log
    INFO("[dbfs.flush %p:%p] ino=%lu", fop, req, ino);
    
    // and reply...
    if ((err = -fuse_reply_err(req, 0)))
        EWARNING(err, "fuse_reply_err");

    // done
    if ((err = dbfs_op_req_done(&fop->base)))
        goto error;

    // good
    return;

error:
    dbfs_op_fail(&fop->base, err);
}

void dbfs_release (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi) {
    // just passthrough to dbfs_op
    // the lo_fd will be closed automatically
    dbfs_op_release(req, ino, fi);
}
