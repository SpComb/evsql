#define _GNU_SOURCE
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "signals.h"
#include "log.h"

struct signals {
    struct event_base *ev_base;

    struct signal {
        struct event *ev;
    } sig_list[MAX_SIGNALS];

    int sig_count;
};

void signals_loopexit (int signal, short event, void *arg) {
    struct signals *signals = arg;

    INFO("[signal] caught %s: exiting the event loop", strsignal(signal));
    
    if (event_base_loopexit(signals->ev_base, NULL))
        FATAL("event_base_loopexit");
}

void signals_ignore (int signal, short event, void *arg) {
    struct signals *signals = arg;

    (void) signals;
    
    /* ignore */
}

struct signals *signals_alloc (struct event_base *ev_base) {
    struct signals *signals = NULL;

    if ((signals = calloc(1, sizeof(*signals))) == NULL)
        ERROR("calloc");
    
    // simple attributes
    signals->ev_base = ev_base;

    // done
    return signals;

error:
    return NULL;
}

int signals_add (struct signals *signals, int sigval, void (*sig_handler)(evutil_socket_t, short, void *)) {
    struct signal *sig_info;
    
    // find our sig_info
    assert(signals->sig_count < MAX_SIGNALS);
    sig_info = &signals->sig_list[signals->sig_count++];
    
    // set up the libevent signal events
    if ((sig_info->ev = signal_new(signals->ev_base, sigval, sig_handler, signals)) == NULL)
        PERROR("signal_new");

    if (signal_add(sig_info->ev, NULL))
        PERROR("signal_add");

    // success
    return 0;

error:
    return -1;
}

struct signals *signals_default (struct event_base *ev_base) {
    struct signals *signals = NULL;
    
    // alloc signals
    if ((signals = signals_alloc(ev_base)) == NULL)
        return NULL;
    
    // add the set of default signals
    if (    signals_add(signals,    SIGPIPE,    &signals_ignore)
        ||  signals_add(signals,    SIGINT,     &signals_loopexit)
    ) ERROR("signals_add");
    
    // success
    return signals;

error:
    if (signals)
        signals_free(signals);

    return NULL;
}   

void signals_free (struct signals *signals) {
    int i;
    
    // free all events
    for (i = 0; i < signals->sig_count; i++) {
        if (signal_del(signals->sig_list[i].ev))
            PWARNING("signal_del");

    }
    
    // free the info itself
    free(signals);
}

