file(GLOB dashboard_tests CONFIGURE_DEPENDS *.cpp)
ap_add_test(NAME ap_dashboard_ut SOURCES ${dashboard_tests} DEPENDS ap_dashboard_core LABELS dashboard)
