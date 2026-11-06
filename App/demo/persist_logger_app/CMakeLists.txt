add_executable(persist_logger_app main.cpp)
target_link_libraries(persist_logger_app PRIVATE ara_per_lib ara_log_lib ara_core_lib ap::warnings ap::sanitizers ap::coverage)
set_target_properties(persist_logger_app PROPERTIES FOLDER "demo")
install(TARGETS persist_logger_app RUNTIME DESTINATION bin OPTIONAL)
