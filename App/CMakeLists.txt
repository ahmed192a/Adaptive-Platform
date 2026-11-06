#################################### Main APP (Execution Management)
add_executable(${PROJECT_NAME} exec_main.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_link_libraries(${PROJECT_NAME} PUBLIC ara_exec_lib ara_log_legacy_lib
                                       PRIVATE ap::legacy ap::sanitizers ap::coverage)

add_subdirectory(SD)
add_subdirectory(SM)
add_subdirectory(UCM)
add_subdirectory(OTA)
add_subdirectory(examples)
add_subdirectory(demo)
add_subdirectory(dashboard)

add_subdirectory(demo/uds_tester)
