# secure_vault: key slots + AES-GCM seal/unseal + signatures + X.509 chain, with a readable trace.
if(AP_HAVE_CRYPTO)
  add_executable(secure_vault main.cpp)
  target_link_libraries(secure_vault PRIVATE ara_crypto_lib ara_per_lib ara_core_lib ap::warnings ap::sanitizers ap::coverage)
  set_target_properties(secure_vault PROPERTIES FOLDER "demo")
  install(TARGETS secure_vault RUNTIME DESTINATION bin)
endif()
