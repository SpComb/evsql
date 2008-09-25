
/*
 * error handling
 */

void _generic_err ( /*int level, */ int use_stderr, const char *func, int perr, const char *fmt, ...)
        __attribute__ ((format (printf, 4, 5)));

// needs to be defined as its own function for the noreturn attribute
void _generic_err_exit ( /* int level, */ int used_stderr, const char *func, int perr, const char *fmt, ...)
        __attribute__ ((format (printf, 4, 5)))
        __attribute__ ((noreturn));

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
#define info(...)                   _generic_err(       0,  NULL,   0,  __VA_ARGS__ )
#define error(...)                  _generic_err(       1,  NULL,   0,  __VA_ARGS__ )
#define err_exit(...)               _generic_err_exit(  1,  NULL,   0,  __VA_ARGS__ )
#define perr(...)                   _generic_err(       1,  NULL,   1,  __VA_ARGS__ )
#define perr_exit(...)              _generic_err_exit(  1,  NULL,   1,  __VA_ARGS__ )
#define err_func(func, ...)         _generic_err(       1,  func,   0,  __VA_ARGS__ )
#define err_func_exit(func, ...)    _generic_err_exit(  1,  func,   0,  __VA_ARGS__ )
#define perr_func(func, ...)        _generic_err(       1,  func,   1,  __VA_ARGS__ )
#define perr_func_exit(func, ...)   _generic_err_exit(  1,  func,   1,  __VA_ARGS__ )

// error(func + colon + msg, ...) + goto error
#define ERROR(...) do { err_func(__func__, __VA_ARGS__); goto error; } while (0)
#define PERROR(...) do { perr_func(__func__, __VA_ARGS__); goto error; } while (0)
#define FATAL(...) err_func_exit(__func__, __VA_ARGS__)
#define PFATAL(...) perr_func_exit(__func__, __VA_ARGS__)
#define WARNING(...) err_func(__func__, __VA_ARGS__)
#define PWARNING(...) perr_func(__func__, __VA_ARGS__)

#ifdef DEBUG_ENABLED
#define DEBUG(...) err_func(__func__, __VA_ARGS__)
#else
#define DEBUG(...) (void) (0)
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
#define INFO(...) (void) (0)
#endif

