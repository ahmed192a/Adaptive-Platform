file(GLOB tsync_tests CONFIGURE_DEPENDS *.cpp)
ap_add_test(NAME ara_tsync_ut SOURCES ${tsync_tests} DEPENDS ara_tsync_lib LABELS tsync)
