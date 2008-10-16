#include <stdlib.h>
#include <assert.h>

#include <postgresql/libpq/libpq-fs.h>

#include "dbfs.h"
#include "op_base.h"
#include "../lib/log.h"

struct dbfs_fileop {
    struct dbfs_op base;

    uint32_t lo_fd;
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
        ||  evsql_result_uint32(res, 0, 1, &fop->lo_fd,     0 ) // fd
    )
        SERROR(err = EIO);

    // is it a dir?
    if (_dbfs_mode(type) != S_IFREG)
        EERROR(err = ENOENT, "wrong type: %s", type);
    
    INFO("[dbfs.open %p:%p] -> ino=%lu, type=%s", fop, fop->base.req, (unsigned long int) fop->base.ino, type);
    
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
        " inodes.type, lo_open(inodes.data, $1::int4) AS fd"
        " FROM inodes"
        " WHERE inodes.ino = $2::int4";

    static struct evsql_query_params params = EVSQL_PARAMS(EVSQL_FMT_BINARY) {
        EVSQL_PARAM ( UINT32 ),
        EVSQL_PARAM ( UINT32 ),

        EVSQL_PARAMS_END
    };

    // build params
    if (0
        ||  evsql_param_uint32(&params, 0, INV_READ | INV_WRITE)
        ||  evsql_param_uint32(&params, 1, fop->base.ino)
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
        if ((err = fuse_reply_err(req, err)))
            EWARNING(err, "fuse_reply_err");
    }
}

void dbfs_read_res (const struct evsql_result_info *res, void *arg) {
    struct dbfs_fileop *fop = arg;
    int err;
    const char *buf;
    size_t size;
 
    // check the results
    if ((err = _dbfs_check_res(res, 1, 1)) < 0)
        SERROR(err = EIO);
        
    // get the data
    if (evsql_result_buf(res, 0, 0, &buf, &size, 0))
        SERROR(err = EIO);

    INFO("[dbfs.read %p:%p] -> size=%zu", fop, fop->base.req, size);
        
    // send it
    if ((err = fuse_reply_buf(fop->base.req, buf, size)))
        EERROR(err, "fuse_reply_buf");
    
    // ok, req handled
    if ((err = dbfs_op_req_done(&fop->base)))
        goto error;

    // good, fallthrough
    err = 0;

error:
    if (err)
        dbfs_op_fail(&fop->base, err);

    // free
    evsql_result_free(res);
}

void dbfs_read (struct fuse_req *req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi) {
    struct dbfs *ctx = fuse_req_userdata(req);
    struct dbfs_fileop *fop;
    int err;
    
    // get the op
    if ((fop = (struct dbfs_fileop *) dbfs_op_req(req, ino, fi)) == NULL)
        return;

    // log
    INFO("[dbfs.read %p:%p] ino=%lu, size=%zu, off=%lu, fi->flags=%04X", fop, req, ino, size, off, fi->flags);

    // query
    const char *sql = 
        "SELECT"
        " lo_pread($1::int4, $2::int4, $3::int4)";
    
    static struct evsql_query_params params = EVSQL_PARAMS(EVSQL_FMT_BINARY) {
        EVSQL_PARAM ( UINT32 ), // fd
        EVSQL_PARAM ( UINT32 ), // len
        EVSQL_PARAM ( UINT32 ), // off

        EVSQL_PARAMS_END
    };

    // build params
    if (0
        ||  evsql_param_uint32(&params, 0, fop->lo_fd)
        ||  evsql_param_uint32(&params, 1, size)
        ||  evsql_param_uint32(&params, 2, off)
    )
        SERROR(err = EIO);
        
    // query
    if (evsql_query_params(ctx->db, fop->base.trans, sql, &params, dbfs_read_res, fop) == NULL)
        SERROR(err = EIO);

    // ok, wait for the info results
    return;

error:
    // fail it
    dbfs_op_fail(&fop->base, err);
}

void dbfs_write (struct fuse_req *req, fuse_ino_t ino, const char *buf, size_t size, off_t off, struct fuse_file_info *fi) {

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
    if ((err = fuse_reply_err(req, 0)))
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
