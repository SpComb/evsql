
#include "dbfs.h"
#include "../lib/log.h"
#include "../lib/misc.h"

// max. size for a setattr UPDATE query
#define DBFS_SETATTR_SQL_MAX 512

// for building the setattr UPDATE
#define FIELD(to_set, flag, field, value) ((to_set) & (flag)) ? (field " = " value ", ") : ""

void _dbfs_attr_res (const struct evsql_result_info *res, void *arg) {
    struct fuse_req *req = arg;
    struct stat st; ZINIT(st);
    int err = 0;
    
    uint32_t ino;

    // check the results
    if ((err = _dbfs_check_res(res, 1, 5)))
        SERROR(err = (err ==  1 ? ENOENT : EIO));
        
    // get our data
    if (0
        ||  evsql_result_uint32(res, 0, 0, &ino,        0 ) // inodes.ino
    )
        EERROR(err = EIO, "invalid db data");
 
        
    INFO("\t[dbfs.getattr %p] -> ino=%lu, stat follows", req, (unsigned long int) ino);
    
    // inode
    st.st_ino = ino;

    // stat attrs
    if ((err = _dbfs_stat_info(&st, res, 0, 1)))
        goto error;

    // reply
    if ((err = fuse_reply_attr(req, &st, st.st_nlink ? CACHE_TIMEOUT : 0)))
        EERROR(err, "fuse_reply_entry");

error:
    if (err && (err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");

    // free
    evsql_result_free(res);
}

void dbfs_getattr (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi) {
    struct dbfs *ctx = fuse_req_userdata(req);
    int err;
    
    (void) fi;

    INFO("[dbfs.getattr %p] ino=%lu", req, ino);

    const char *sql =
        "SELECT"
        " inodes.ino, " DBFS_STAT_COLS
        " FROM inodes"
        " WHERE inodes.ino = $1::int4";

    static struct evsql_query_params params = EVSQL_PARAMS(EVSQL_FMT_BINARY) {
        EVSQL_PARAM ( UINT32 ),

        EVSQL_PARAMS_END
    };

    // build params
    if (0
        ||  evsql_param_uint32(&params, 0, ino)
    )
        SERROR(err = EIO);
        
    // query
    if (evsql_query_params(ctx->db, NULL, sql, &params, _dbfs_attr_res, req) == NULL)
        SERROR(err = EIO);

    // XXX: handle interrupts
    
    // wait
    return;

error:
    if ((err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");
}


void dbfs_setattr (struct fuse_req *req, fuse_ino_t ino, struct stat *attr, int to_set, struct fuse_file_info *fi) {
    struct dbfs *ctx = fuse_req_userdata(req);
    int err;
    int ret;
    
    char sql_buf[DBFS_SETATTR_SQL_MAX];
    
    static struct evsql_query_params params = EVSQL_PARAMS(EVSQL_FMT_BINARY) {
        EVSQL_PARAM ( UINT16 ), // inodes.mode
        EVSQL_PARAM ( UINT32 ), // inodes.uid
        EVSQL_PARAM ( UINT32 ), // inodes.gid
        EVSQL_PARAM ( UINT32 ), // data size
        EVSQL_PARAM ( UINT32 ), // ino

        EVSQL_PARAMS_END
    };

    // log
    INFO("[dbfs.setattr %p] ino=%lu, fileop=%p: ", req, ino, fi && fi->fh ? (void*) fi->fh : NULL);
    
    if (to_set & FUSE_SET_ATTR_MODE) {
        // ignore the S_IFMT
        attr->st_mode &= 07777;

        INFO("\tmode    = %08o", attr->st_mode);
    }

    if (to_set & FUSE_SET_ATTR_UID)
        INFO("\tuid     = %u", attr->st_uid);

    if (to_set & FUSE_SET_ATTR_GID)
        INFO("\tgid     = %u", attr->st_gid);

    if (to_set & FUSE_SET_ATTR_SIZE)
        INFO("\tsize    = %lu", attr->st_size);

    if (to_set & FUSE_SET_ATTR_ATIME)
        INFO("\tatime   = %lu", attr->st_atime);

    if (to_set & FUSE_SET_ATTR_MTIME)
        INFO("\tmtime   = %lu", attr->st_mtime);

    // the SQL
    if ((ret = snprintf(sql_buf, DBFS_SETATTR_SQL_MAX,
        "UPDATE inodes SET"
        " %s%s%s%s ino = ino"
        " WHERE inodes.ino = $5::int4"
        " RETURNING inodes.ino, " DBFS_STAT_COLS,
        
        FIELD(to_set, FUSE_SET_ATTR_MODE,   "mode", "$1::int2"),
        FIELD(to_set, FUSE_SET_ATTR_UID,    "uid",  "$2::int4"),
        FIELD(to_set, FUSE_SET_ATTR_GID,    "gid",  "$3::int4"),
        FIELD(to_set, FUSE_SET_ATTR_SIZE,   "data", "lo_otruncate(data, $4::int4)")
    )) >= DBFS_SETATTR_SQL_MAX && (err = EIO))
        ERROR("sql_buf is too small: %i", ret);
    
    // the params...
    if (0
        || (                                  evsql_params_clear(&params)                    )
        || ((to_set & FUSE_SET_ATTR_MODE ) && evsql_param_uint16(&params, 0, attr->st_mode)  )
        || ((to_set & FUSE_SET_ATTR_UID  ) && evsql_param_uint32(&params, 1, attr->st_uid)   )
        || ((to_set & FUSE_SET_ATTR_GID  ) && evsql_param_uint32(&params, 2, attr->st_gid)   )
        || ((to_set & FUSE_SET_ATTR_SIZE ) && evsql_param_uint32(&params, 3, attr->st_size)  )
        || (                                  evsql_param_uint32(&params, 4, ino)            )
    )
        SERROR(err = EIO);

    // trace the query
    evsql_query_debug(sql_buf, &params);
    
    // query... we can pretend it's a getattr :)
    if (evsql_query_params(ctx->db, NULL, sql_buf, &params, _dbfs_attr_res, req) == NULL)
        SERROR(err = EIO);

    // XXX: handle interrupts
    
    // wait
    return;

error:
    if ((err = fuse_reply_err(req, err)))
        EWARNING(err, "fuse_reply_err");
}
