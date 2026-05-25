#add the test file and name add_executable(Name_of_test File_to_run)
add_executable(benchmark_cypher cypherbench.cpp)
add_executable(benchmark_postgis postbench.cpp)
add_executable(benchmark_finalBench finalBench.cpp)

#uses the same name as the first arg of this function form the root CMakeLists.txt-> 
#target_include_directories(some_lib PUBLIC include)
target_link_libraries(benchmark_cypher PRIVATE some_lib)
target_link_libraries(benchmark_postgis PRIVATE some_lib)
target_link_libraries(benchmark_finalBench PRIVATE some_lib)

#adds test add_test(name name_to_look_after_in_terminal command same_name_as_above)
add_test(NAME benchmark_cypher_function COMMAND benchmark_cypher)
add_test(NAME benchmark_postgis_function COMMAND benchmark_postgis)
add_test(NAME benchmark_finalBench_function COMMAND benchmark_finalBench)