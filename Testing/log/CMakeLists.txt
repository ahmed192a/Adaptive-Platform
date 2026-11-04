file(GLOB log_tests CONFIGURE_DEPENDS *.cpp)
ap_add_test(NAME ara_log_ut SOURCES ${log_tests} DEPENDS ara_log_lib LABELS log)
