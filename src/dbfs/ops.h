#ifndef DBFS_OPS_H
#define DBFS_OPS_H

#include "../evfuse.h"

/* dbfs.c */
void dbfs_init (void *userdata, struct fuse_conn_info *conn);
void dbfs_destroy (void *arg);

/* core.c */
void dbfs_lookup (struct fuse_req *req, fuse_ino_t parent, const char *name);

/* attr.c */
void dbfs_getattr (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi);
void dbfs_setattr(struct fuse_req *req, fuse_ino_t ino, struct stat *attr, int to_set, struct fuse_file_info *fi);

/* link.c */
void dbfs_readlink (struct fuse_req *req, fuse_ino_t ino);

/* dirop.c */
void dbfs_opendir (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi);
void dbfs_readdir (struct fuse_req *req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi);
void dbfs_releasedir (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi);

/* mk.c */
void dbfs_mknod (struct fuse_req *req, fuse_ino_t parent, const char *name, mode_t mode, dev_t rdev);
void dbfs_mkdir (struct fuse_req *req, fuse_ino_t parent, const char *name, mode_t mode);
void dbfs_symlink (struct fuse_req *req, const char *link, fuse_ino_t parent, const char *name);

/* tree.c */
void dbfs_rename (struct fuse_req *req, fuse_ino_t parent, const char *name, fuse_ino_t newparent, const char *newname);

/* fileop.c */
void dbfs_open (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi);
void dbfs_read (struct fuse_req *req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi);
void dbfs_write (struct fuse_req *req, fuse_ino_t ino, const char *buf, size_t size, off_t off, struct fuse_file_info *fi);
void dbfs_flush (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi);
void dbfs_release (struct fuse_req *req, fuse_ino_t ino, struct fuse_file_info *fi);

#endif /* DBFS_OPS_H */
