# everything except main() is a library so the unit tests can link it
add_library(ap_dashboard_core STATIC http_server.cpp platform_node.cpp dash_features.cpp)
target_include_directories(ap_dashboard_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ap_dashboard_core PUBLIC
  ara_core_lib ara_log_lib ara_per_lib ara_phm_lib ara_diag_lib
  ara_nm_lib ara_tsync_lib uds_scenario ota_updater_core
  nlohmann_json::nlohmann_json Threads::Threads
  PRIVATE ap::warnings ap::sanitizers ap::coverage)
if(AP_HAVE_CRYPTO)
  target_link_libraries(ap_dashboard_core PUBLIC ara_crypto_lib)
  target_compile_definitions(ap_dashboard_core PUBLIC AP_NODE_CRYPTO=1)
endif()
add_executable(ap_dashboard main.cpp)
target_link_libraries(ap_dashboard PRIVATE ap_dashboard_core ap::warnings ap::sanitizers ap::coverage)
set_target_properties(ap_dashboard ap_dashboard_core PROPERTIES FOLDER "apps")
# the dashboard files live next to the binary
add_custom_target(ap_dashboard_web
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/web ${CMAKE_CURRENT_BINARY_DIR}/web
  COMMENT "Copying dashboard web files")
add_dependencies(ap_dashboard ap_dashboard_web)
if(AP_INSTALL)
  install(TARGETS ap_dashboard RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(DIRECTORY web DESTINATION ${CMAKE_INSTALL_DATADIR}/adaptive-platform/dashboard)
endif()
