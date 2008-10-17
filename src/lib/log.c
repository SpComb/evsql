#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "log.h"

static void _generic_err_vargs (int flags, const char *func, int err, const char *fmt, va_list va) {
    FILE *stream = flags & LOG_DISPLAY_STDERR ? stderr : stdout;

    if (!fmt)
        return;

    if (flags & LOG_DISPLAY_FATAL)
        fprintf(stream, "FATAL: ");

    if (func)
        fprintf(stream, "%s: ", func);
    
    vfprintf(stream, fmt, va);
    
    if (flags & LOG_DISPLAY_PERR)
        fprintf(stream, ": %s\n", strerror(err == 0 ? errno : err));
    
    if (!(flags & LOG_DISPLAY_NONL))
        fprintf(stream, "\n");
}

void _generic_err (int flags, const char *func, int err, const char *fmt, ...) {
    va_list va;

    va_start(va, fmt);
    _generic_err_vargs(flags, func, err, fmt, va);
    va_end(va);
}

void _generic_err_exit (int flags, const char *func, int err, const char *fmt, ...) {
    va_list va;

    assert(fmt);

    va_start(va, fmt);
    _generic_err_vargs(flags | LOG_DISPLAY_FATAL, func, err, fmt, va);
    va_end(va);
      
    exit(EXIT_FAILURE);
}

