file(GLOB sm_tests CONFIGURE_DEPENDS *.cpp)
ap_add_test(NAME ara_sm_ut SOURCES ${sm_tests} DEPENDS ara_sm_lib LABELS sm)
