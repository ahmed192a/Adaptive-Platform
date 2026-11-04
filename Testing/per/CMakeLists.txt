file(GLOB per_tests CONFIGURE_DEPENDS *.cpp)
ap_add_test(NAME ara_per_ut SOURCES ${per_tests} DEPENDS ara_per_lib LABELS per)
