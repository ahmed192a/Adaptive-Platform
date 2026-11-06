#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "ara/core/future.h"
#include "ara/core/span.h"

namespace {
using ara::core::Span;

TEST(SpanTest, ConstructionAndAccess) {
  std::vector<int> v{1, 2, 3, 4, 5};
  Span<int> s(v);
  EXPECT_EQ(s.size(), 5U);
  EXPECT_EQ(s.size_bytes(), 5U * sizeof(int));
  EXPECT_FALSE(s.empty());
  EXPECT_EQ(s[2], 3);
  s[2] = 30;
  EXPECT_EQ(v[2], 30);
  int arr[3] = {7, 8, 9};
  Span<int, 3> fixed(arr);
  EXPECT_EQ(fixed.size(), 3U);
  EXPECT_EQ(Span<int>(arr, arr + 2).size(), 2U);
  EXPECT_TRUE(Span<int>().empty());
  std::array<int, 2> const a{4, 5};
  Span<const int> cs(a);
  EXPECT_EQ(cs[1], 5);
}

TEST(SpanTest, SubviewsAndIteration) {
  std::vector<int> v{1, 2, 3, 4, 5};
  Span<int> s = ara::core::MakeSpan(v);
  EXPECT_EQ(s.first(2).size(), 2U);
  EXPECT_EQ(s.last(2)[0], 4);
  EXPECT_EQ(s.subspan(1, 3)[0], 2);
  EXPECT_EQ(s.subspan(3).size(), 2U);
  EXPECT_EQ(s.first<3>().size(), 3U);
  EXPECT_EQ(s.last<1>()[0], 5);
  EXPECT_EQ((s.subspan<1, 2>()[1]), 3);
  int sum = 0;
  for (int x : s) sum += x;
  EXPECT_EQ(sum, 15);
  EXPECT_EQ(*s.rbegin(), 5);
  EXPECT_EQ(*s.crbegin(), 5);
  Span<const int> conv = s;  // Span<int> -> Span<const int>
  EXPECT_EQ(conv.size(), 5U);
}

TEST(PromiseTest, SwapExchangesSharedState) {
  ara::core::Promise<int> a, b;
  auto fa = a.get_future();
  auto fb = b.get_future();
  a.Swap(b);  // a now fulfils fb's state, b fulfils fa's
  a.set_value(2);
  b.set_value(1);
  EXPECT_EQ(fa.get(), 1);
  EXPECT_EQ(fb.get(), 2);
  ara::core::Promise<int> c, d;
  auto fc = c.get_future();
  swap(c, d);
  d.set_value(9);
  EXPECT_EQ(fc.get(), 9);
}

}  // namespace
