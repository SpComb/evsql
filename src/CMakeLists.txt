# add our include path
include_directories (${evsql_SOURCE_DIR}/include ${LibEvent_INCLUDE_DIRS})

# define our source code modules
set (LIB_SOURCES "lib/log.c")
set (EVPQ_SOURCES evpq.c)
set (EVSQL_SOURCES core.c util.c)

# XXX: silly cmake does silly things when you SET with only one arg
set (SOURCES lib/log.c evpq.c core.c util.c)

# add our library
add_library (evsql STATIC ${SOURCES})
target_link_libraries (evsql ${LibEvent_LIBRARIES})

