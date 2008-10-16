
#include <stdlib.h>
#include <assert.h>

#include "dbfs.h"
#include "op_base.h"
#include "../dirbuf.h"
#include "../lib/log.h"

/*
 * Directory related functionality like opendir, readdir, releasedir
 */
struct dbfs_dirop {
    struct dbfs_op base;

    // parent dir inodes
    uint32_t parent;
    
    // for readdir
    struct dirbuf dirbuf;
};

/*
 * Release the dirbuf.
 */
static void _dbfs_dirop_free (struct dbfs_op *op_base) {
    struct dbfs_dirop *dirop = (struct dbfs_dirop *) op_base;

    // just release the dirbuf
    dirbuf_release(&dirop->dirbuf);
}

/*
 * Handle the results for the initial attribute lookup for the dir itself during opendir ops.
 */
static void dbfs_opendir_res (const struct evsql_result_info *res, void *arg) {
    struct dbfs_dirop *dirop = arg;
    int err;
    
    assert(dirop->base.req);
    assert(dirop->base.trans); // query callbacks don't get called if the trans fails
    assert(!dirop->base.open);
   
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
    
    INFO("[dbfs.opendir %p:%p] -> ino=%lu, parent=%lu, type=%s", dirop, dirop->base.req, (unsigned long int) dirop->base.ino, (unsigned long int) dirop->parent, type);
    
    // open_fn done, do the open_reply
    if ((err = dbfs_op_open_reply(&dirop->base)))
        goto error;

    // success, fallthrough for evsql_result_free
    err = 0;

error:
    if (err)
        // fail it
        dbfs_op_fail(&dirop->base, err);
    
    // free
    evsql_result_free(res);
}

/*
 * The opendir transaction is ready for use. Query for the given dir's info
 */
static void dbfs_dirop_open (struct dbfs_op *op_base) {
    struct dbfs_dirop *dirop = (struct dbfs_dirop *) op_base;
    struct dbfs *ctx = fuse_req_userdata(dirop->base.req);
    int err;
    
    assert(dirop->base.trans); 
    assert(dirop->base.req);
    assert(!dirop->base.open);

    INFO("[dbfs.opendir %p:%p] -> trans=%p", dirop, dirop->base.req, dirop->base.trans);
    
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
        ||  evsql_param_uint32(&params, 0, dirop->base.ino)
    )
        SERROR(err = EIO);
        
    // query
    if (evsql_query_params(ctx->db, dirop->base.trans, sql, &params, dbfs_opendir_res, dirop) == NULL)
        SERROR(err = EIO);

    // ok, wait for the info results
    return;

error:
    // fail it
    dbfs_op_fail(&dirop->base, err);
}

/*
 * Handle opendir(), this means starting a new op.
 */
void dbfs_opendir (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi) {
    struct dbfs *ctx = fuse_req_userdata(req);
    struct dbfs_dirop *dirop = NULL;
    int err;
    
    // allocate it
    if ((dirop = calloc(1, sizeof(*dirop))) == NULL && (err = EIO))
        ERROR("calloc");

    // do the op_open
    if ((err = dbfs_op_open(ctx, &dirop->base, req, ino, fi, _dbfs_dirop_free, dbfs_dirop_open)))
        ERROR("dbfs_op_open");

    INFO("[dbfs.opendir %p:%p] ino=%lu, fi=%p", dirop, req, ino, fi);
    
    // wait
    return;

error:
    if (dirop) {
        // we can fail normally
        dbfs_op_fail(&dirop->base, err);

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
static void dbfs_readdir_res (const struct evsql_result_info *res, void *arg) {
    struct dbfs_dirop *dirop = arg;
    int err;
    size_t row;
    
    assert(dirop->base.req);
    assert(dirop->base.trans); // query callbacks don't get called if the trans fails
    assert(dirop->base.open);
    
    // check the results
    if ((err = _dbfs_check_res(res, 0, 4)) < 0)
        SERROR(err = EIO);
        
    INFO("[dbfs.readdir %p:%p] -> files: res_rows=%zu", dirop, dirop->base.req, evsql_result_rows(res));
        
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
        if ((err = dirbuf_add(dirop->base.req, &dirop->dirbuf, off + 2, off + 3, name, ino, _dbfs_mode(type))) < 0 && (err = EIO))
            ERROR("failed to add dirent for inode=%lu", (long unsigned int) ino);
        
        // stop if it's full
        if (err > 0)
            break;
    }

    // send it
    if ((err = dirbuf_done(dirop->base.req, &dirop->dirbuf)))
        EERROR(err, "failed to send buf");
    
    // handled the req
    if ((err = dbfs_op_req_done(&dirop->base)))
        goto error;

    // good, fallthrough
    err = 0;

error:
    if (err)
        dbfs_op_fail(&dirop->base, err);

    // free
    evsql_result_free(res);
}

/*
 * Handle a readdir request. This will execute a SQL query inside the transaction to get the files at the given offset,
 * and dbfs_readdir_res will handle the results.
 *
 * If trans failed earlier, detect that and return an error.
 */
void dbfs_readdir (struct fuse_req *req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi) {
    struct dbfs *ctx = fuse_req_userdata(req);
    struct dbfs_dirop *dirop;
    int err;

    // get the op
    if ((dirop = (struct dbfs_dirop *) dbfs_op_req(req, ino, fi)) == NULL)
        return;
    
    INFO("[dbfs.readdir %p:%p] ino=%lu, size=%zu, off=%zu, fi=%p : trans=%p", dirop, req, ino, size, off, fi, dirop->base.trans);

    // create the dirbuf
    if (dirbuf_init(&dirop->dirbuf, size, off))
        SERROR(err = EIO);

    // add . and ..
    // we set the next offset to 2, because all dirent offsets will be larger than that
    // assume that these two should *always* fit
    if ((err = (0
        ||  dirbuf_add(req, &dirop->dirbuf, 0, 1, ".",   dirop->base.ino,    S_IFDIR )
        ||  dirbuf_add(req, &dirop->dirbuf, 1, 2, "..",  
                        dirop->parent ? dirop->parent : dirop->base.ino,     S_IFDIR )
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
        ||  evsql_param_uint32(&params, 0, ino)
        ||  evsql_param_uint32(&params, 1, off)
        ||  evsql_param_uint32(&params, 2, dirbuf_estimate(&dirop->dirbuf, 0))
    )
        SERROR(err = EIO);

    // query
    if (evsql_query_params(ctx->db, dirop->base.trans, sql, &params, dbfs_readdir_res, dirop) == NULL)
        SERROR(err = EIO);

    // good, wait
    return;

error:
    dbfs_op_fail(&dirop->base, err);
}

/*
 * "For every [succesfull] opendir call there will be exactly one releasedir call."
 *
 * The dirop may be in a failed state.
 */
void dbfs_releasedir (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi) {
    // just passthrough to dbfs_op
    dbfs_op_release(req, ino, fi);
}

