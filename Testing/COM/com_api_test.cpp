// Tests for the spec-shaped ara::com additions: ComErrc, the proxy event sample queue,
// SOME/IP session handling and SD reboot detection.
#include "ara/com/SOMEIP/session.h"
#include "ara/com/com_error_domain.h"
#include "ara/com/proxy_skeleton/proxy/event_buffer.h"

#include <gtest/gtest.h>

#include <chrono>
#include <set>
#include <thread>
#include <vector>

using namespace ara::com;
using namespace ara::com::someip;

// ------------------------------------------------------------------- ComErrc
TEST(ComErrorDomain, CodesCarryNameMessageAndDomain) {
  auto ec = MakeErrorCode(ComErrc::kServiceNotAvailable);
  EXPECT_STREQ(ec.Domain().Name(), "Com");
  EXPECT_EQ(ec.Value(), 1);
  EXPECT_STREQ(ec.Message(), "Service is not available");
  EXPECT_EQ(&ec.Domain(), &GetComDomain());
}

TEST(ComErrorDomain, EveryEnumeratorHasADistinctValueAndAMessage) {
  std::set<int> values;
  for (int i = 1; i <= 21; ++i) {
    auto ec = MakeErrorCode(static_cast<ComErrc>(i));
    values.insert(static_cast<int>(ec.Value()));
    EXPECT_STRNE(ec.Message(), "Unknown error") << i;
  }
  EXPECT_EQ(values.size(), 21u);
  EXPECT_STREQ(MakeErrorCode(static_cast<ComErrc>(99)).Message(), "Unknown error");
}

TEST(ComErrorDomain, ThrowAsExceptionThrowsComException) {
  EXPECT_THROW(GetComDomain().ThrowAsException(MakeErrorCode(ComErrc::kBindingFailure)), ComException);
}

// -------------------------------------------------------------- event buffer
namespace {
struct Fixture {
  ProxyEventBuffer<int> ev{8};
  std::vector<SubscriptionState> states;
  Fixture() { ev.SetSubscriptionStateChangeHandler([this](SubscriptionState s) { states.push_back(s); }); }
  void Subscribed(std::size_t n) {
    ASSERT_TRUE(ev.Subscribe(n).HasValue());
    ev.OnSubscriptionAck(true);
  }
  std::vector<int> Drain(std::size_t max = 1000) {
    std::vector<int> out;
    auto r = ev.GetNewSamples([&](SamplePtr<int> p) { out.push_back(*p); }, max);
    EXPECT_TRUE(r.HasValue());
    return out;
  }
};
} // namespace

TEST(ComEvent, StartsNotSubscribedAndDropsSamples) {
  Fixture f;
  EXPECT_EQ(f.ev.GetSubscriptionState(), SubscriptionState::kNotSubscribed);
  f.ev.Push(1);
  EXPECT_EQ(f.ev.GetFreeSampleCount(), 0u);
  EXPECT_TRUE(f.Drain().empty());
}

TEST(ComEvent, SubscribeGoesPendingThenSubscribedOnAck) {
  Fixture f;
  ASSERT_TRUE(f.ev.Subscribe(4).HasValue());
  EXPECT_EQ(f.ev.GetSubscriptionState(), SubscriptionState::kSubscriptionPending);
  f.ev.OnSubscriptionAck(true);
  EXPECT_EQ(f.ev.GetSubscriptionState(), SubscriptionState::kSubscribed);
  f.ev.Unsubscribe();
  EXPECT_EQ(f.states, (std::vector<SubscriptionState>{SubscriptionState::kSubscriptionPending,
                                                       SubscriptionState::kSubscribed,
                                                       SubscriptionState::kNotSubscribed}));
}

TEST(ComEvent, MaxSampleCountMustBeRealizable) {
  Fixture f;
  auto zero = f.ev.Subscribe(0);
  ASSERT_FALSE(zero.HasValue());
  EXPECT_EQ(zero.Error(), MakeErrorCode(ComErrc::kMaxSampleCountNotRealizable));
  EXPECT_FALSE(f.ev.Subscribe(9).HasValue()); // binding limit is 8
  EXPECT_EQ(f.ev.GetSubscriptionState(), SubscriptionState::kNotSubscribed);
}

TEST(ComEvent, SubscribeTwiceIsANoOp) {
  Fixture f;
  f.Subscribed(4);
  EXPECT_TRUE(f.ev.Subscribe(2).HasValue());
  EXPECT_EQ(f.ev.GetSubscriptionState(), SubscriptionState::kSubscribed);
  EXPECT_EQ(f.ev.GetFreeSampleCount(), 4u);
}

TEST(ComEvent, SamplesAreDeliveredOldestFirst) {
  Fixture f;
  f.Subscribed(4);
  f.ev.Push(1);
  f.ev.Push(2);
  f.ev.Push(3);
  EXPECT_EQ(f.Drain(), (std::vector<int>{1, 2, 3}));
  EXPECT_TRUE(f.Drain().empty());
}

TEST(ComEvent, GetNewSamplesHonoursTheMaximum) {
  Fixture f;
  f.Subscribed(8);
  for (int i = 0; i < 5; ++i) f.ev.Push(i);
  EXPECT_EQ(f.Drain(2), (std::vector<int>{0, 1}));
  EXPECT_EQ(f.Drain(), (std::vector<int>{2, 3, 4}));
}

TEST(ComEvent, FullQueueDropsTheOldestSample) {
  Fixture f;
  f.Subscribed(3);
  for (int i = 1; i <= 5; ++i) f.ev.Push(i);
  EXPECT_EQ(f.Drain(), (std::vector<int>{3, 4, 5}));
}

TEST(ComEvent, FreeSampleCountTracksQueuedAndHeldSamples) {
  Fixture f;
  f.Subscribed(4);
  EXPECT_EQ(f.ev.GetFreeSampleCount(), 4u);
  f.ev.Push(1);
  f.ev.Push(2);
  EXPECT_EQ(f.ev.GetFreeSampleCount(), 2u);
  std::vector<SamplePtr<int>> keep;
  auto r = f.ev.GetNewSamples([&](SamplePtr<int> p) { keep.push_back(p); });
  ASSERT_TRUE(r.HasValue());
  EXPECT_EQ(r.Value(), 2u);
  EXPECT_EQ(f.ev.GetFreeSampleCount(), 2u); // 2 now held by the application
  keep.clear();
  EXPECT_EQ(f.ev.GetFreeSampleCount(), 4u);
}

TEST(ComEvent, HoldingTooManySamplesIsMaxSamplesReached) {
  Fixture f;
  f.Subscribed(2);
  std::vector<SamplePtr<int>> keep;
  f.ev.Push(1);
  f.ev.Push(2);
  ASSERT_TRUE(f.ev.GetNewSamples([&](SamplePtr<int> p) { keep.push_back(p); }).HasValue());
  f.ev.Push(3); // all slots held: lost
  auto r = f.ev.GetNewSamples([&](SamplePtr<int> p) { keep.push_back(p); });
  ASSERT_FALSE(r.HasValue());
  EXPECT_EQ(r.Error(), MakeErrorCode(ComErrc::kMaxSamplesReached));
  keep.pop_back();
  f.ev.Push(4);
  auto r2 = f.ev.GetNewSamples([&](SamplePtr<int> p) { keep.push_back(p); });
  ASSERT_TRUE(r2.HasValue());
  EXPECT_EQ(*keep.back(), 4);
}

TEST(ComEvent, ReceiveHandlerFiresOnEachSampleUntilUnset) {
  Fixture f;
  f.Subscribed(4);
  int calls = 0;
  f.ev.SetReceiveHandler([&] { ++calls; });
  f.ev.Push(1);
  f.ev.Push(2);
  EXPECT_EQ(calls, 2);
  f.ev.UnsetReceiveHandler();
  f.ev.Push(3);
  EXPECT_EQ(calls, 2);
}

TEST(ComEvent, UnsubscribeDiscardsQueuedSamplesButKeepsHeldOnesValid) {
  Fixture f;
  f.Subscribed(4);
  f.ev.Push(1);
  f.ev.Push(2);
  SamplePtr<int> held;
  f.ev.GetNewSamples([&](SamplePtr<int> p) { if (!held) held = p; }, 1);
  f.ev.Unsubscribe();
  EXPECT_EQ(*held, 1);
  EXPECT_TRUE(f.Drain().empty());
  EXPECT_EQ(f.ev.GetFreeSampleCount(), 0u);
}

TEST(ComEvent, RefusedSubscriptionStaysPending) {
  Fixture f;
  ASSERT_TRUE(f.ev.Subscribe(4).HasValue());
  f.ev.OnSubscriptionAck(false);
  EXPECT_EQ(f.ev.GetSubscriptionState(), SubscriptionState::kSubscriptionPending);
  f.ev.Push(1);
  EXPECT_TRUE(f.Drain().empty());
}

TEST(ComEvent, ProducerAndConsumerThreadsDoNotLoseOrCorruptSamples) {
  ProxyEventBuffer<int> ev(256);
  ASSERT_TRUE(ev.Subscribe(256).HasValue());
  ev.OnSubscriptionAck(true);
  std::thread prod([&] { for (int i = 0; i < 2000; ++i) ev.Push(i); });
  int last = -1;
  bool ordered = true;
  std::size_t total = 0;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while ((total < 1 || prod.joinable()) && std::chrono::steady_clock::now() < deadline) {
    auto r = ev.GetNewSamples([&](SamplePtr<int> p) { ordered &= *p > last; last = *p; });
    total += r.Value();
    if (last == 1999) break;
    std::this_thread::yield();
  }
  prod.join();
  ev.GetNewSamples([&](SamplePtr<int> p) { ordered &= *p > last; last = *p; });
  EXPECT_TRUE(ordered);
  EXPECT_EQ(last, 1999);
}

// ------------------------------------------------------------------ session
TEST(SomeIpSession, StartsAtOneAndWrapsFromFFFFToOne) { // PRS_SOMEIP_00933 / 00521
  SessionIdCounter c;
  EXPECT_EQ(c.Next(), 1);
  EXPECT_EQ(c.Next(), 2);
  for (int i = 3; i <= 0xFFFF; ++i) c.Next();
  EXPECT_EQ(c.Next(), 1);
}

TEST(SomeIpSession, InactiveSessionHandlingAlwaysUsesZero) { // PRS_SOMEIP_00932
  SessionIdCounter c(false);
  EXPECT_EQ(c.Next(), 0);
  EXPECT_EQ(c.Next(), 0);
}

TEST(SomeIpSession, RebootFlagIsSetUntilTheFirstWrap) { // PRS_SOMEIPSD_00255
  SessionIdCounter c;
  EXPECT_TRUE(c.RebootFlag());
  for (int i = 1; i < 0xFFFF; ++i) c.Next();
  EXPECT_TRUE(c.RebootFlag());
  c.Next(); // 0xFFFF
  EXPECT_FALSE(c.RebootFlag());
  c.Reset();
  EXPECT_TRUE(c.RebootFlag());
  EXPECT_EQ(c.Next(), 1);
}

TEST(SomeIpSession, ConcurrentCallersGetUniqueIds) {
  SessionIdCounter c;
  std::vector<std::thread> ts;
  std::vector<std::vector<int>> got(4);
  for (int t = 0; t < 4; ++t)
    ts.emplace_back([&, t] { for (int i = 0; i < 1000; ++i) got[t].push_back(c.Next()); });
  for (auto &t : ts) t.join();
  std::set<int> all;
  for (auto &g : got) all.insert(g.begin(), g.end());
  EXPECT_EQ(all.size(), 4000u);
}

// --------------------------------------------------------- request matching
TEST(SomeIpRequests, ResponseCompletesTheMatchingRequestOnly) { // PRS_SOMEIP_00533 / 00739
  RequestTracker rt;
  auto a = rt.Register(5, 1);
  auto b = rt.Register(5, 2);
  EXPECT_EQ(rt.Pending(), 2u);
  EXPECT_TRUE(rt.Complete(5, 2, 0, {9, 9}));
  auto rb = b.get();
  EXPECT_EQ(rb.payload, (std::vector<std::uint8_t>{9, 9}));
  EXPECT_EQ(a.wait_for(std::chrono::milliseconds(10)), std::future_status::timeout);
  EXPECT_TRUE(rt.Complete(5, 1, 0x0A, {}));
  EXPECT_EQ(a.get().returnCode, 0x0A);
  EXPECT_EQ(rt.Pending(), 0u);
}

TEST(SomeIpRequests, UnsolicitedOrDuplicateResponsesAreRejected) {
  RequestTracker rt;
  EXPECT_FALSE(rt.Complete(1, 1, 0, {}));
  auto f = rt.Register(1, 1);
  EXPECT_TRUE(rt.Complete(1, 1, 0, {}));
  EXPECT_FALSE(rt.Complete(1, 1, 0, {}));
  f.get();
}

TEST(SomeIpRequests, SameClientAndSessionCannotBePendingTwice) {
  RequestTracker rt;
  auto a = rt.Register(1, 1);
  auto b = rt.Register(1, 1);
  EXPECT_TRUE(a.valid());
  EXPECT_FALSE(b.valid());
}

TEST(SomeIpRequests, CancelAndCancelAllYieldCancelledResponses) {
  RequestTracker rt;
  auto a = rt.Register(1, 1);
  auto b = rt.Register(1, 2);
  auto c = rt.Register(2, 1);
  EXPECT_TRUE(rt.Cancel(1, 1));
  EXPECT_FALSE(rt.Cancel(1, 1));
  EXPECT_TRUE(a.get().cancelled);
  rt.CancelAll();
  EXPECT_TRUE(b.get().cancelled);
  EXPECT_TRUE(c.get().cancelled);
  EXPECT_EQ(rt.Pending(), 0u);
}

// --------------------------------------------------------------- SD reboot
TEST(SdReboot, FirstMessageIsNeverAReboot) {
  SdRebootDetector d;
  EXPECT_FALSE(d.Observe(1, false, true, 1));
}

TEST(SdReboot, NormalSessionIncreaseIsNoReboot) { // PRS_SOMEIPSD_00258
  SdRebootDetector d;
  d.Observe(1, false, true, 1);
  EXPECT_FALSE(d.Observe(1, false, true, 2));
  EXPECT_FALSE(d.Observe(1, false, true, 3));
}

TEST(SdReboot, RebootFlagGoingFromClearedToSetIsAReboot) {
  SdRebootDetector d;
  d.Observe(1, false, false, 40);
  EXPECT_TRUE(d.Observe(1, false, true, 1));
}

TEST(SdReboot, SessionIdNotIncreasingWithFlagSetIsAReboot) {
  SdRebootDetector d;
  d.Observe(1, false, true, 7);
  EXPECT_TRUE(d.Observe(1, false, true, 1));
  EXPECT_TRUE(d.Observe(1, false, true, 1));
}

TEST(SdReboot, WrapWithClearedFlagIsNoReboot) {
  SdRebootDetector d;
  d.Observe(1, false, true, 0xFFFF);
  EXPECT_FALSE(d.Observe(1, false, false, 1)); // peer wrapped: flag cleared
  EXPECT_FALSE(d.Observe(1, false, false, 2));
}

TEST(SdReboot, UnicastAndMulticastAreTrackedSeparately) { // PRS_SOMEIPSD_00256
  SdRebootDetector d;
  d.Observe(1, false, true, 10);
  d.Observe(1, true, true, 3);
  EXPECT_FALSE(d.Observe(1, false, true, 11));
  EXPECT_FALSE(d.Observe(1, true, true, 4));
}

TEST(SdReboot, PeersAreIndependentAndCanBeForgotten) {
  SdRebootDetector d;
  d.Observe(1, false, true, 10);
  EXPECT_FALSE(d.Observe(2, false, true, 1));
  d.Forget(1);
  EXPECT_FALSE(d.Observe(1, false, true, 1)); // first message after forgetting
}
