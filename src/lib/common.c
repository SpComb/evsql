#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "common.h"

static void _generic_err_vargs (int use_stderr, const char *func, int perr, const char *fmt, va_list va) {
    FILE *stream = use_stderr ? stderr : stdout;

    if (func)
        fprintf(stream, "%s: ", func);
    
    vfprintf(stream, fmt, va);
    
    if (perr)
        fprintf(stream, ": %s\n", strerror(perr > 0 ? errno : -perr));

    fprintf(stream, "\n");
}

void _generic_err (int use_stderr, const char *func, int perr, const char *fmt, ...) {
    va_list va;

    va_start(va, fmt);
    _generic_err_vargs(use_stderr, func, perr, fmt, va);
    va_end(va);
}

void _generic_err_exit (int use_stderr, const char *func, int perr, const char *fmt, ...) {
    va_list va;

    va_start(va, fmt);
    _generic_err_vargs(use_stderr, func, perr, fmt, va);
    va_end(va);
      
    exit(EXIT_FAILURE);
}

