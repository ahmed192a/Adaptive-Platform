add_executable(supervised_service main.cpp)
target_link_libraries(supervised_service PRIVATE ara_phm_lib ara_exec_lib ara_core_lib Threads::Threads
                                                 ap::warnings ap::sanitizers ap::coverage)
set_target_properties(supervised_service PROPERTIES FOLDER "demo")
# Scripted scenario doubles as an integration test: hang -> EXPIRED -> recovery -> OK.
add_test(NAME demo_supervised_service COMMAND supervised_service --scenario)
set_tests_properties(demo_supervised_service PROPERTIES LABELS "demo" TIMEOUT 60)
