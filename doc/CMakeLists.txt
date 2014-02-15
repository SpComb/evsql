# We need Doxygen
find_package (Doxygen)

# path to .dox with general project info
set (PROJECT_DOX_FILE "${CMAKE_CURRENT_SOURCE_DIR}/evsql.dox")

# source file/dirs to generate docs from
set (PROJECT_DOC_SOURCES "${PROJECT_SOURCE_DIR}/src/include/evsql.h")

# generate doxygen output under .../html
set (PROJECT_DOXYGEN_DIR "${PROJECT_SOURCE_DIR}/doc")

IF (DOXYGEN_FOUND)
    # doxygen config path
    set (DOXYGEN_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/doxygen.conf")

    # doxygen.conf.in -> doxygen.conf
    configure_file (
        ${CMAKE_CURRENT_SOURCE_DIR}/doxygen.conf.in
        ${DOXYGEN_CONFIG}
        @ONLY
    )
                    
    # add custom doc target
    ADD_CUSTOM_TARGET(doc
        ${DOXYGEN_EXECUTABLE} ${DOXYGEN_CONFIG}
    )
    
    MESSAGE (STATUS "Doxygen: output will be generated in ${PROJECT_DOXYGEN_DIR}/html")

ELSE (DOXYGEN_FOUND)
    MESSAGE (STATUS "Doxygen: install `doxygen` and rerun cmake to use `make doc`")

ENDIF (DOXYGEN_FOUND)

