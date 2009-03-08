#
# This module looks for Doxygen executable 
# and the Graphviz dot executable 
# which could be used to generate html 
# and graphical documentaton from source code. 
# 
# It will set the following variables:
#
#  DOXYGEN_FOUND
#  DOXYGEN_EXECUTABLE
#
#  DOXYGEN_DOT_FOUND
#  DOXYGEN_DOT_EXECUTABLE
#  DOXYGEN_DOT_EXECUTABLE_PATH
#
# deprecated variables:
#  DOXYGEN
#  DOT
#
# see:
#  www.doxygen.org
#  www.research.att.com/sw/tools/graphviz/
#
# adapted from:
#  www.mip.informatik.uni-kiel.de/~jw/cmake/CMakeModules/FindDoxygen.cmake

FIND_PROGRAM (DOXYGEN_EXECUTABLE
    NAMES doxygen
    DOC "Path to doxygen binary"
    PATHS $ENV{DOXYGEN_HOME}
)
#MESSAGE(STATUS "DBG DOXYGEN_EXECUTABLE=${DOXYGEN_EXECUTABLE}")

FIND_PROGRAM (DOXYGEN_DOT_EXECUTABLE
    NAMES dot
    DOC "Path to dot binary from Graphiz (for doxygen)"
    PATHS $ENV{DOT_HOME}
)
#MESSAGE(STATUS "DBG DOXYGEN_DOT_EXECUTABLE=${DOXYGEN_DOT_EXECUTABLE}")

IF (DOXYGEN_EXECUTABLE)
    SET (DOXYGEN_FOUND TRUE)

    MESSAGE (STATUS "Found Doxygen at ${DOXYGEN_EXECUTABLE}")
ENDIF (DOXYGEN_EXECUTABLE)

IF (DOXYGEN_DOT_EXECUTABLE)
    SET (DOXYGEN_DOT_FOUND TRUE)  
    
    MESSAGE (STATUS "Found Dot at ${DOXYGEN_DOT_EXECUTABLE}")

    # the directory of dot is required in doxygen.config: DOT_PATH
    GET_FILENAME_COMPONENT (DOXYGEN_DOT_EXECUTABLE_PATH ${DOXYGEN_DOT_EXECUTABLE} PATH)

ENDIF (DOXYGEN_DOT_EXECUTABLE)

# hide
MARK_AS_ADVANCED (
    DOXYGEN_EXECUTABLE
    DOXYGEN_DOT_EXECUTABLE
    DPXYGEN_DOT_EXECUTABLE_DIR
    DOXYGEN
    DOT
)

