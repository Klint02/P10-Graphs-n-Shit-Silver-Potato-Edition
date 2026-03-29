cmake_minimum_required(VERSION 3.10)
project(P10_graphs_and_shit VERSION 0.1.0 LANGUAGES C CXX)

# PostgreSQL
find_package(PostgreSQL REQUIRED)

# Library
add_library(some_lib src/DBfunctions.cpp)
target_include_directories(some_lib PUBLIC include src ${PostgreSQL_INCLUDE_DIRS})
target_link_libraries(some_lib PUBLIC ${PostgreSQL_LIBRARIES})

# Executable
add_executable(P10-graphs-and-shit app/main.cpp)
target_link_libraries(P10-graphs-and-shit PRIVATE some_lib)

# Testing
include(CTest)
enable_testing()
add_subdirectory(tests)