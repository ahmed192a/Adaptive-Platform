#include <gtest/gtest.h>
#include "ara/com/marshal.h"
using namespace ara::com;


float test1(int a, float b) {
    return a + b;
}

TEST(MarshalTest, MarshalTest)
{

    Marshal m;
    auto payload = m.serialize_params(1, 2.2f);

    auto args = m.get_args<int, float>(payload);

    auto result = std::apply(test1, args);
    EXPECT_EQ(result, 3.2f);
}