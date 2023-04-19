#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <vector>
#include "ara/com/marshal.h"
using namespace ara::com;


float test1(int a, float b) {
    return a + b;
}

void test2(int a, std::vector<uint8_t> b, int c) {
    std::cout << "test2 a = "<< a << ", c = "<< c << std::endl;

    for (auto i : b) {
        printf("%02x ", (uint8_t)i);
    }
    std::cout << std::endl;


}

TEST(MarshalTest, MarshalTest)
{

    Marshal m;
    auto payload = m.serialize_params(1, 2.2f);

    auto args = m.get_args<int, float>(payload);

    auto result = std::apply(test1, args);
    EXPECT_EQ(result, 3.2f);
}

TEST(MarshalTest, MarshalTest2)
{

    Marshal m;
    std::vector<uint8_t> v = {1, 2, 3, 4, 5};
    auto payload = m.serialize_params(5, v, 20);
    std::cout << "payload size: " << payload.size() << std::endl;
    for (auto i : v) {
        printf("%02x ", (uint8_t)i);
    }
    std::cout << std::endl;
    for (auto i : payload) {
        printf("%02x ", (uint8_t)i);
    }
    std::cout << std::endl;

    auto args = m.get_args<int, std::vector<uint8_t>, int>(payload);

    std::apply(test2, args);
}