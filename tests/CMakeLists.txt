#add the test file and name add_executable(Name_of_test File_to_run)
add_executable(test_run test_run.cpp)

#uses the same name as the first arg of this function form the root CMakeLists.txt-> 
#target_include_directories(some_lib PUBLIC include)
target_link_libraries(test_run PRIVATE some_lib)

#adds test add_test(name name_to_look_after_in_terminal command same_name_as_above)
add_test(NAME test_add_function COMMAND test_run)