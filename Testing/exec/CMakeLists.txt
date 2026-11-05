file(GLOB exec_tests CONFIGURE_DEPENDS *.cpp)
ap_add_test(NAME ara_exec_ut SOURCES ${exec_tests} DEPENDS ara_exec_lib LABELS exec)
