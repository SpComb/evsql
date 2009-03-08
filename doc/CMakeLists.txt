# We need Doxygen
find_package (Doxygen)

# set our .dox path
set (PROJECT_DOX_FILE "${CMAKE_CURRENT_SOURCE_DIR}/evsql.dox")

# where to install doxygen output
set (PROJECT_DOXYGEN_DIR "${PROJECT_SOURCE_DIR}/doc")

IF (DOXYGEN_FOUND)
    # doxygen.conf.in -> doxygen.conf
    configure_file (
        ${CMAKE_CURRENT_SOURCE_DIR}/doxygen.conf.in
        ${CMAKE_CURRENT_BINARY_DIR}/doxygen.conf
        @ONLY
    )
            
    # set doxygen config path
    set (DOXYGEN_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/doxygen.conf")
                    
    # add custom doc target
    ADD_CUSTOM_TARGET(
        doc 
        ${DOXYGEN_EXECUTABLE} ${DOXYGEN_CONFIG}
    )
    
    MESSAGE (STATUS "Doxygen: output will be installed in ${PROJECT_DOXYGEN_DIR}")

ELSE (DOXYGEN_FOUND)
    MESSAGE (STATUS "Doxygen: not found :(")    

ENDIF (DOXYGEN_FOUND)

