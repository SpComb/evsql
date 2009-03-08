# Find libevent
# Once done, this will define:
#
#   LibEvent_FOUND
#   LibEvent_INCLUDE_DIRS
#   LibEvent_LIBRARIES
#
# Currently, this only supports libevent-svn (i.e. 1.5/2.0), so it's kind of useless for real use :)

include (LibFindMacros)

# include dir
find_path (LibEvent_INCLUDE_DIR
    NAMES "event2/event.h"
)

# library
find_library (LibEvent_LIBRARY
    NAMES "event"
)

# set the external vars
set (LibEvent_PROCESS_INCLUDES LibEvent_INCLUDE_DIR)
set (LibEvent_PROCESS_LIBS LibEvent_LIBRARY)
libfind_process (LibEvent)
