
add_executable(Cloud server.cpp metadata.h)
target_link_libraries(Cloud nlohmann_json::nlohmann_json)