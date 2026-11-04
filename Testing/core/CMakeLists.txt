file(GLOB core_tests CONFIGURE_DEPENDS *.cpp)
ap_add_test(NAME ara_core_ut SOURCES ${core_tests} DEPENDS ara_core_lib ara_exec_lib LABELS core)
