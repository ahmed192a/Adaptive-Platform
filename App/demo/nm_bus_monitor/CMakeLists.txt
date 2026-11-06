add_executable(nm_bus_monitor main.cpp)
target_link_libraries(nm_bus_monitor PRIVATE ara_nm_lib ara_core_lib ap::warnings ap::sanitizers ap::coverage)
set_target_properties(nm_bus_monitor PROPERTIES FOLDER "demo")

if(BUILD_TESTING)
  add_test(NAME demo_nm_bus_monitor COMMAND nm_bus_monitor)
  set_tests_properties(demo_nm_bus_monitor PROPERTIES LABELS "demo" PASS_REGULAR_EXPRESSION "summary|synchronized again")
endif()
