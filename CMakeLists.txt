# policy
cmake_minimum_required (VERSION 2.6)

# project attributes
project (evsql C)

# cmake paths
set(CMAKE_MODULE_PATH "${evsql_SOURCE_DIR}/cmake/Modules/")

# dependancies
find_package(LibEvent REQUIRED)

# add the src subdir
add_subdirectory (src)

