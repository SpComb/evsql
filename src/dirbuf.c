
#include <stdlib.h>

#include "dirbuf.h"
#include "lib/log.h"
#include "lib/math.h"

int dirbuf_init (struct dirbuf *buf, size_t req_size, off_t req_off) {
    buf->buf = NULL;
    buf->len = req_size;
    buf->off = 0;
    buf->req_off = req_off;
    
    DEBUG("\tdirbuf.init: req_size=%zu", req_size);

    // allocate the mem
    if ((buf->buf = malloc(buf->len)) == NULL)
        ERROR("malloc");
    
    // ok
    return 0;

error:
    return -1;
}

size_t dirbuf_estimate (struct dirbuf *buf, size_t min_namelen) {
    char namebuf[DIRBUF_NAME_MAX];
    int i;
    
    // build a dummy string of the right length
    for (i = 0; i < min_namelen && i < DIRBUF_NAME_MAX - 1; i++)
        namebuf[i] = 'x';

    namebuf[i] = '\0';

    return buf->len / (fuse_add_direntry(NULL, NULL, 0, namebuf, NULL, 0));
}

int dirbuf_add (fuse_req_t req, struct dirbuf *buf, off_t ent_off, off_t next_off, const char *ent_name, fuse_ino_t ent_ino, mode_t ent_mode) {
    struct stat stbuf;
    size_t ent_size;

    DEBUG("\tdirbuf.add: req_off=%zu, buf->len=%zu, buf->off=%zu, ent_off=%zu, next_off=%zu, ent_name=`%s`, ent_ino=%lu, ent_mode=%07o",
        buf->req_off, buf->len, buf->off, ent_off, next_off, ent_name, ent_ino, ent_mode);
    
    // skip entries as needed
    if (ent_off < buf->req_off) 
        return 0;

    // set ino
    stbuf.st_ino = ent_ino;
    stbuf.st_mode = ent_mode;
    
    // try and add the dirent, and see if it fits
    if ((ent_size = fuse_add_direntry(req, buf->buf + buf->off, buf->len - buf->off, ent_name, &stbuf, next_off)) > (buf->len - buf->off)) {
        // 'tis full
        return 1;

    } else {
        // it fit
        buf->off += ent_size;
    }

    // success
    return 0;
}

int dirbuf_done (fuse_req_t req, struct dirbuf *buf) {
    int err;
    
    // send the reply, return the error later
    err = -fuse_reply_buf(req, buf->buf, buf->off);

    DEBUG("\tdirbuf.done: size=%zu/%zu, err=%d", buf->off, buf->len, err);

    // free the dirbuf
    dirbuf_release(buf);

    // return the error code
    return err;
}

void dirbuf_release (struct dirbuf *buf) {
    free(buf->buf); buf->buf = NULL;
}

