#ifndef SIMPLE_H
#define SIMPLE_H

/*
 * A simple static in-memory filesystem structure.
 */

#include <fuse/fuse_lowlevel.h>

/*
 * A simple file/dir.
 */
struct simple_node {
    // inode number
    fuse_ino_t inode;

    // mode
    mode_t mode_type;
    mode_t mode_perm;

    // parent node
    fuse_ino_t parent;

    // name
    const char *name;
    
    // data
    const char *data;
};

/*
 * General information.
 */
struct simple_fs;

/*
 * Initialize simple, and get the fuse_lowlevel_ops.
 */
struct fuse_lowlevel_ops *simple_init ();

/*
 * Create a new simple_fs.
 */
struct simple_fs *simple_new (const struct simple_node *node_list);

#endif /* SIMPLE_H */
