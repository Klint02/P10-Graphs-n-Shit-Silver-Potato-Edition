cmake_minimum_required(VERSION 3.10)
set (CMAKE_CXX_STANDARD 23)
project(P10_graphs_and_shit VERSION 0.1.0 LANGUAGES C CXX)

# YAML CPP
include(FetchContent)

FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG master # Can be a tag (yaml-cpp-x.x.x), a commit hash, or a branch name (master)
)
FetchContent_MakeAvailable(yaml-cpp)


# PostgreSQL
find_package(PostgreSQL REQUIRED)



# Library
add_library(some_lib src/DBfunctions.cpp)
target_include_directories(some_lib PUBLIC include src ${PostgreSQL_INCLUDE_DIRS})
target_link_libraries(some_lib PRIVATE PostgreSQL::PostgreSQL)
target_link_libraries(some_lib PUBLIC yaml-cpp::yaml-cpp) # The library or executable that require yaml-cpp library

# Executable
add_executable(P10-graphs-and-shit app/main.cpp)
target_link_libraries(P10-graphs-and-shit PRIVATE some_lib)


# Testing
include(CTest)
enable_testing()
add_subdirectory(tests)