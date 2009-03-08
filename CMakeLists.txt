# policy
cmake_minimum_required (VERSION 2.6)

# project attributes
project (evsql C)

# cmake paths
set(CMAKE_MODULE_PATH "${evsql_SOURCE_DIR}/cmake/Modules/")

# add the subdirs
add_subdirectory (src)
add_subdirectory (doc)

