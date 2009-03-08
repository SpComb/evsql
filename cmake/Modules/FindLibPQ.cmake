# Find libevent
# Once done, this will define:
#
#   LibPQ_FOUND
#   LibPQ_INCLUDE_DIRS
#   LibPQ_LIBRARIES
#
# Currently, this only supports libevent-svn (i.e. 1.5/2.0), so it's kind of useless for real use :)

include (LibFindMacros)

# include dir
find_path (LibPQ_INCLUDE_DIR
    NAMES "postgresql/libpq-fe.h"
    PATHS "$ENV{POSTGRESQL_PREFIX}/include"
)

# library
find_library (LibPQ_LIBRARY
    NAMES "pq"
    PATHS "$ENV{POSTGRESQL_PREFIX}/lib"
)

# set the external vars
set (LibPQ_PROCESS_INCLUDES LibPQ_INCLUDE_DIR)
set (LibPQ_PROCESS_LIBS LibPQ_LIBRARY)
libfind_process (LibPQ)
