add_executable(time_sync_clock main.cpp)
target_link_libraries(time_sync_clock PRIVATE ara_tsync_lib ara_core_lib ap::warnings ap::sanitizers ap::coverage)
set_target_properties(time_sync_clock PROPERTIES FOLDER "demo")

if(BUILD_TESTING)
  add_test(NAME demo_time_sync_clock COMMAND time_sync_clock)
  set_tests_properties(demo_time_sync_clock PROPERTIES LABELS "demo" PASS_REGULAR_EXPRESSION "summary|synchronized again")
endif()
