
/*
 * A simple PostgreSQL-based filesystem.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <event2/event.h>

#include "dbfs.h"
#include "evfuse.h"
#include "evsql.h"
#include "lib/log.h"
#include "lib/signals.h"
#include "lib/misc.h"

#define CONNINFO_DEFAULT "dbname=dbfs port=5433"

int main (int argc, char **argv) {
    struct event_base *ev_base = NULL;
    struct signals *signals = NULL;
    struct dbfs *ctx = NULL;
    const char *db_conninfo;
    struct fuse_args fuse_args = FUSE_ARGS_INIT(argc, argv);
    
    // parse args, XXX: fuse_args
    db_conninfo = CONNINFO_DEFAULT;
    
    // init libevent
    if ((ev_base = event_base_new()) == NULL)
        ERROR("event_base_new");
    
    // setup signals
    if ((signals = signals_default(ev_base)) == NULL)
        ERROR("signals_default");

    // setup dbfs
    if ((ctx = dbfs_new(ev_base, &fuse_args, db_conninfo)) == NULL)
        ERROR("dbfs_new");

    // run libevent
    INFO("running libevent loop");

    if (event_base_dispatch(ev_base))
        PERROR("event_base_dispatch");
    
    // clean shutdown

error :
    if (ctx)
        dbfs_free(ctx);
    
    if (signals)
        signals_free(signals);

    if (ev_base)
        event_base_free(ev_base);
    
    fuse_opt_free_args(&fuse_args);
}

