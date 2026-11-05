file(GLOB diag_tests CONFIGURE_DEPENDS *.cpp)
ap_add_test(NAME ara_diag_ut SOURCES ${diag_tests} DEPENDS ara_diag_lib LABELS diag)
