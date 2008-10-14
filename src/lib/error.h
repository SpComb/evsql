#ifndef LIB_ERROR_H
#define LIB_ERROR_H

#include "log.h"

#define ERROR(...) do { err_func(__func__, __VA_ARGS__); goto error; } while (0)
#define PERROR(...) do { perr_func(__func__, __VA_ARGS__); goto error; } while (0)
#define EERROR(_err, ...) do { eerr_func(__func__, (_err), __VA_ARGS__); goto error; } while (0)
#define NERROR(...) do { err_func_nonl(__func__, __VA_ARGS__); goto error; } while (0)

// XXX: replace with *err_func(...) + exit(EXIT_FAILURE)
#define FATAL(...) err_func_exit(__func__, __VA_ARGS__)
#define PFATAL(...) perr_func_exit(__func__, __VA_ARGS__)
#define EFATAL(_err, ...) eerr_func_exit(__func__, (_err), __VA_ARGS__)

#endif /* LIB_ERROR_H */
