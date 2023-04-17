include_directories(${P7_Headers})

set(SOURCE11
   p7.cpp
)

add_executable(p7-cpp-example11 ${SOURCE11})
target_link_libraries(p7-cpp-example11 P7)