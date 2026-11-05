file(GLOB nm_tests CONFIGURE_DEPENDS *.cpp)
ap_add_test(NAME ara_nm_ut SOURCES ${nm_tests} DEPENDS ara_nm_lib LABELS nm)
