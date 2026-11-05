add_executable(phm_demo main.cpp)
target_link_libraries(phm_demo PRIVATE ara_phm_lib ara_log_lib ap::warnings ap::sanitizers ap::coverage)
set_target_properties(phm_demo PROPERTIES FOLDER "examples")
