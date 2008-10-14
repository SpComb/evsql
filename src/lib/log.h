#ifndef LIB_LOG_H
#define LIB_LOG_H

/*
 * error handling
 */

enum log_display_flags {
    LOG_DISPLAY_STDOUT =    0x00,
    LOG_DISPLAY_STDERR =    0x01,

    LOG_DISPLAY_PERR =      0x02,

    LOG_DISPLAY_NONL =      0x04,

    LOG_DISPLAY_FATAL =     0x08,
};


void _generic_err (int flags, const char *func, int err, const char *fmt, ...)
        __attribute__ ((format (printf, 4, 5)));

// needs to be defined as its own function for the noreturn attribute
void _generic_err_exit (int flags, const char *func, int err, const char *fmt, ...)
        __attribute__ ((format (printf, 4, 5)))
        __attribute__ ((noreturn));

static inline void debug_dummy (int dummy, ...) { /* no-op */ }

enum _debug_level {
    DEBUG_FATAL,
    DEBUG_ERROR,
    DEBUG_WARNING,
    DEBUG_INFO,
    DEBUG_DEBUG,
};

// not currently used
extern enum _debug_level _cur_debug_level;

// various kinds of ways to handle an error, 2**3 of them, *g*
#define info(...)                   _generic_err(       LOG_DISPLAY_STDOUT,                     NULL, 0,    __VA_ARGS__ )
#define error(...)                  _generic_err(       LOG_DISPLAY_STDERR,                     NULL, 0,    __VA_ARGS__ )
#define err_exit(...)               _generic_err_exit(  LOG_DISPLAY_STDERR,                     NULL, 0,    __VA_ARGS__ )
#define perr(...)                   _generic_err(       LOG_DISPLAY_STDERR | LOG_DISPLAY_PERR,  NULL, 0,    __VA_ARGS__ )
#define perr_exit(...)              _generic_err_exit(  LOG_DISPLAY_STDERR | LOG_DISPLAY_PERR,  NULL, 0,    __VA_ARGS__ )
#define err_func(func, ...)         _generic_err(       LOG_DISPLAY_STDERR,                     func, 0,    __VA_ARGS__ )
#define err_func_nonl(func, ...)    _generic_err(       LOG_DISPLAY_STDERR | LOG_DISPLAY_NONL,  func, 0,    __VA_ARGS__ )
#define err_func_exit(func, ...)    _generic_err_exit(  LOG_DISPLAY_STDERR,                     func, 0,    __VA_ARGS__ )
#define perr_func(func, ...)        _generic_err(       LOG_DISPLAY_STDERR | LOG_DISPLAY_PERR,  func, 0,    __VA_ARGS__ )
#define perr_func_exit(func, ...)   _generic_err_exit(  LOG_DISPLAY_STDERR | LOG_DISPLAY_PERR,  func, 0,    __VA_ARGS__ )
#define eerr_func(func, err, ...)   _generic_err(       LOG_DISPLAY_STDERR | LOG_DISPLAY_PERR,  func, err,  __VA_ARGS__ )
#define eerr_func_exit(func,err,...) _generic_err_exit( LOG_DISPLAY_STDERR | LOG_DISPLAY_PERR,  func, err,  __VA_ARGS__ )
#define debug(func, ...)            _generic_err(       LOG_DISPLAY_STDERR,                     func, 0,    __VA_ARGS__ )
#define debug_nonl(func, ...)       _generic_err(       LOG_DISPLAY_STDERR | LOG_DISPLAY_NONL,  func, 0,    __VA_ARGS__ )

// logging includes errors
#include "error.h"

#define WARNING(...) err_func(__func__, __VA_ARGS__)
#define NWARNING(...) err_func_nonl(__func__, __VA_ARGS__)
#define PWARNING(...) perr_func(__func__, __VA_ARGS__)
#define EWARNING(err, ...) eerr_func(__func__, (err), __VA_ARGS__)

#ifdef DEBUG_ENABLED
#define DEBUG(...) debug(__func__, __VA_ARGS__)
#define DEBUGF(...) debug(NULL, __VA_ARGS__)
#define DEBUGN(...) debug_nonl(__func__, __VA_ARGS__)
#define DEBUGNF(...) debug_nonl(NULL, __VA_ARGS__)
#else
#define DEBUG(...) debug_dummy(0, __VA_ARGS__)
#define DEBUGF(...) debug_dummy(0, __VA_ARGS__)
#define DEBUGN(...) debug_dummy(0, __VA_ARGS__)
#define DEBUGNF(...) debug_dummy(0, __VA_ARGS__)
#endif

// default is to enable INFO
#ifdef INFO_DISABLED
    #define INFO_ENABLED 0
#else
    #ifndef INFO_ENABLED
        #define INFO_ENABLED 1
    #endif
#endif

#if INFO_ENABLED
#define INFO(...) info(__VA_ARGS__)
#else
#define INFO(...) (void) (__VA_ARGS__)
#endif

#endif /* LIB_LOG_H */
