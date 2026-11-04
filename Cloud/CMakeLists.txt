add_executable(Cloud server.cpp metadata.h)
target_link_libraries(Cloud PRIVATE nlohmann_json::nlohmann_json ap::legacy)
