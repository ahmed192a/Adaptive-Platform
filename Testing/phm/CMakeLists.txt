file(GLOB phm_tests CONFIGURE_DEPENDS *.cpp)
ap_add_test(NAME ara_phm_ut SOURCES ${phm_tests} DEPENDS ara_phm_lib LABELS phm)
