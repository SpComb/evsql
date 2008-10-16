#ifndef DBFS_OPS_H
#define DBFS_OPS_H

#include "../evfuse.h"

/* dbfs.c */
void dbfs_init (void *userdata, struct fuse_conn_info *conn);
void dbfs_destroy (void *arg);

/* core.c */
void dbfs_lookup (struct fuse_req *req, fuse_ino_t parent, const char *name);
void dbfs_getattr (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi);

/* dirop.c */
void dbfs_opendir (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi);
void dbfs_readdir (struct fuse_req *req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi);
void dbfs_releasedir (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi);

#endif /* DBFS_OPS_H */