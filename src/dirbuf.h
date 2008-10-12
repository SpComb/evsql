#ifndef DIRBUF_H
#define DIRBUF_H

/*
 * Simple dirent building
 */

#include "evfuse.h"

/*
 * Holds the dir entries
 */ 
struct dirbuf {
    char *buf;
    size_t len;
    off_t off, req_off;
};

// maximum length for a dirbuf name, including NUL byte
#define DIRBUF_NAME_MAX 256

/*
 * Initialize a dirbuf for a request. The dirbuf will be filled with at most req_size bytes of dir entries.
 *
 *  req_size    - how many bytes of dirbuf data we want, at most 
 *  req_off     - the offset of the first dirent to include
 */
int dirbuf_init (struct dirbuf *buf, size_t req_size, off_t req_off);

/*
 * Estimate how many dir entries will, at most, fit into a difbuf of the given size, based on a minimum filename size.
 */
size_t dirbuf_estimate (struct dirbuf *buf, size_t min_namelen);

/*
 * Add an dir entry to the dirbuf. The dirbuf should not be full.
 *
 * Offsets are followed:
 *  ent_off     - the offset of this dirent
 *  next_off    - the offset of the next dirent
 *
 * Only the S_IFMT bits of ent_mode are relevant.
 *
 * Returns 0 if the ent was added or skipped, -1 on error, 1 if the dirbuf is full (no more ents should be added).
 */
int dirbuf_add (fuse_req_t req, struct dirbuf *buf, off_t ent_off, off_t next_off, const char *ent_name, fuse_ino_t ent_ino, mode_t ent_mode);

/*
 * Attempt to send the readdir reply, free the buf, and return the error code from fuse_reply_buf
 */
int dirbuf_done (fuse_req_t req, struct dirbuf *buf);

/*
 * Release the dirop without sending any reply back.
 *
 * This is safe to be called multiple times.
 */
void dirbuf_release (struct dirbuf *buf);

#endif /* DIRBUF_H */
