add_executable(p7-cpp-example11 p7.cpp)
target_include_directories(p7-cpp-example11 PRIVATE ${P7_Headers})
target_link_libraries(p7-cpp-example11 PRIVATE P7)

add_executable(CLI cli.cpp)
target_link_libraries(CLI PRIVATE CLI11::CLI11)
