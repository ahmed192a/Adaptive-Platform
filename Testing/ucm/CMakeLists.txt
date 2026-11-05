file(GLOB ucm_tests CONFIGURE_DEPENDS *.cpp)
ap_add_test(NAME ara_ucm_ut SOURCES ${ucm_tests} DEPENDS ara_ucm_lib LABELS ucm)
