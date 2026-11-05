file(GLOB crypto_tests CONFIGURE_DEPENDS *.cpp)
ap_add_test(NAME ara_crypto_ut SOURCES ${crypto_tests} DEPENDS ara_crypto_lib LABELS crypto)
