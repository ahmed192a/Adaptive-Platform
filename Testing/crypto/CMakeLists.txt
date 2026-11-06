file(GLOB crypto_tests CONFIGURE_DEPENDS *.cpp)
ap_add_test(NAME ara_crypto_ut SOURCES ${crypto_tests} DEPENDS ara_crypto_lib LABELS crypto)

# End-to-end smoke test of the secure_vault demo application (registered here: App/ is added before enable_testing()).
if(TARGET secure_vault)
  add_test(NAME secure_vault_demo COMMAND secure_vault demo ${CMAKE_CURRENT_BINARY_DIR}/vault_data)
  set_tests_properties(secure_vault_demo PROPERTIES LABELS "demo;crypto" TIMEOUT 60)
endif()
