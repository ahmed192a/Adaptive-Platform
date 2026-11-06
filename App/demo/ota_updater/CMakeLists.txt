# In-process software update through the UCM PackageManagement rules and State Management.
add_library(ota_updater_core STATIC ota_updater.cpp)
target_include_directories(ota_updater_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ota_updater_core PUBLIC ara_ucm_lib ara_sm_lib
                      PRIVATE ap::warnings ap::sanitizers ap::coverage)
add_executable(ota_updater main.cpp)
target_link_libraries(ota_updater PRIVATE ota_updater_core ap::warnings ap::sanitizers ap::coverage)
set_target_properties(ota_updater ota_updater_core PROPERTIES FOLDER "apps")
if(AP_INSTALL)
  install(TARGETS ota_updater RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
