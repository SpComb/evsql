# dependancies
find_package (LibEvent REQUIRED)
find_package (LibPQ REQUIRED)

# add our include path
include_directories ("include")

# define our source code modules
set (LIB_SOURCES "lib/log.c")
set (EVPQ_SOURCES evpq.c)
set (EVSQL_SOURCES core.c util.c)

# XXX: silly cmake does silly things when you SET with only one arg
set (EVSQL_SOURCES lib/log.c evpq.c core.c query.c result.c util.c)
set (EVSQL_LIBRARIES ${LibEvent_LIBRARIES} ${LibPQ_LIBRARIES})

# compiler flags
set (CFLAGS "-Wall -Wextra")

# add our library
add_library (evsql ${EVSQL_SOURCES})

# set target attributes
target_link_libraries (evsql ${EVSQL_LIBRARIES})
set_target_properties (evsql PROPERTIES
    FRAMEWORK       True
    PUBLIC_HEADER   include/evsql.h
)

# setup install info
install (TARGETS evsql
    LIBRARY         DESTINATION lib
    ARCHIVE         DESTINATION lib/static
    PUBLIC_HEADER   DESTINATION include
)

# test stuff
add_executable (evsql_test EXCLUDE_FROM_ALL lib/log.c lib/signals.c evsql_test.c)
target_link_libraries (evsql_test evsql)

# global target properties
set_target_properties (evsql evsql_test PROPERTIES
    COMPILE_FLAGS   ${CFLAGS}
)

