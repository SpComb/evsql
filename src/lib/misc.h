#ifndef LIB_UTIL_H
#define LIB_UTIL_H

#include <string.h>
#include <arpa/inet.h>

/*
 * Initialize the given *value* with zeros
 */
#define ZINIT(obj) memset(&(obj), 0, sizeof((obj)))

/*
 * 64-bit hton{s,l,q}
 */
#ifndef WORDS_BIGENDIAN /* i.e. if (little endian) */
#define htonq(x) (((uint64_t)htonl((x)>>32))|(((uint64_t)htonl(x))<<32))
#define ntohq(x) htonq(x)
#else
#define htonq(x) ((uint64_t)(x))
#define ntohq(x) ((uint64_t)(x))
#endif


#endif /* LIB_UTIL_H */

