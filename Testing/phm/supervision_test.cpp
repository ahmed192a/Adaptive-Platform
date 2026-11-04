#include "phm_test_util.h"

using namespace phmtest;
using S = ElementarySupervisionStatus;

// ---------------------------------------------------------------------------
// Alive supervision
// ---------------------------------------------------------------------------
TEST(AliveSupervision, StartsDeactivatedAndIgnoresInput) {
  auto s = MakeAlive();
  EXPECT_EQ(s->Status(), S::kDEACTIVATED);
  s->Evaluate(T(1000));
  EXPECT_EQ(s->Status(), S::kDEACTIVATED);
}

TEST(AliveSupervision, CorrectIndicationsKeepOk) {
  auto s = MakeAlive("A", 2);
  s->Activate(T(0));
  for (int cycle = 0; cycle < 5; ++cycle) {
    s->OnCheckpoint(CP("E", 1), T(cycle * 100 + 10));
    s->OnCheckpoint(CP("E", 1), T(cycle * 100 + 60));
    s->Evaluate(T((cycle + 1) * 100));
    EXPECT_EQ(s->Status(), S::kOK) << "cycle " << cycle;
  }
}

TEST(AliveSupervision, MarginsAreHonoured) {
  auto s = MakeAlive("A", /*expected*/ 4, /*min*/ 1, /*max*/ 2);
  s->Activate(T(0));
  auto cycle = [&](int n, int idx) {
    for (int i = 0; i < n; ++i) s->OnCheckpoint(CP("E", 1), T(idx * 100 + 5));
    s->Evaluate(T((idx + 1) * 100));
  };
  cycle(3, 0); // 3 = 4-1 ok
  EXPECT_EQ(s->Status(), S::kOK);
  cycle(6, 1); // 6 = 4+2 ok
  EXPECT_EQ(s->Status(), S::kOK);
  cycle(2, 2); // too few
  EXPECT_EQ(s->Status(), S::kEXPIRED); // tolerance 0
}

TEST(AliveSupervision, TooManyIndicationsFailToo) {
  auto s = MakeAlive("A", 1);
  s->Activate(T(0));
  for (int i = 0; i < 5; ++i) s->OnCheckpoint(CP("E", 1), T(10));
  s->Evaluate(T(100));
  EXPECT_EQ(s->Status(), S::kEXPIRED);
}

TEST(AliveSupervision, ToleranceGivesFailedThenRecoveryThenExpiry) {
  auto s = MakeAlive("A", 1, 0, 0, /*tolerance*/ 2);
  s->Activate(T(0));
  // cycle 0: miss -> FAILED (1 <= 2)
  s->Evaluate(T(100));
  EXPECT_EQ(s->Status(), S::kFAILED);
  // cycle 1: correct -> counter back to 0 -> OK
  s->OnCheckpoint(CP("E", 1), T(150));
  s->Evaluate(T(200));
  EXPECT_EQ(s->Status(), S::kOK);
  // three misses in a row: 1 FAILED, 2 FAILED, 3 > tolerance -> EXPIRED
  s->Evaluate(T(300));
  EXPECT_EQ(s->Status(), S::kFAILED);
  s->Evaluate(T(400));
  EXPECT_EQ(s->Status(), S::kFAILED);
  s->Evaluate(T(500));
  EXPECT_EQ(s->Status(), S::kEXPIRED);
}

TEST(AliveSupervision, ExpiredIsStickyUntilReactivated) {
  auto s = MakeAlive("A", 1);
  s->Activate(T(0));
  s->Evaluate(T(100));
  ASSERT_EQ(s->Status(), S::kEXPIRED);
  s->OnCheckpoint(CP("E", 1), T(110));
  s->Evaluate(T(200));
  EXPECT_EQ(s->Status(), S::kEXPIRED);
  s->Deactivate();
  EXPECT_EQ(s->Status(), S::kDEACTIVATED);
  s->Activate(T(300));
  EXPECT_EQ(s->Status(), S::kOK);
}

TEST(AliveSupervision, LateEvaluationCatchesUpAllCycles) {
  auto s = MakeAlive("A", 1, 0, 0, /*tolerance*/ 3);
  s->Activate(T(0));
  s->Evaluate(T(250)); // two whole cycles elapsed without any indication
  EXPECT_EQ(s->Status(), S::kFAILED);
  EXPECT_EQ(static_cast<AliveSupervision &>(*s).FailedCycles(), 2U);
  s->Evaluate(T(450)); // two more -> 4 > 3
  EXPECT_EQ(s->Status(), S::kEXPIRED);
}

TEST(AliveSupervision, CheckpointIsAttributedToItsOwnCycleEvenIfEvaluateIsLate) {
  // Cycle 0 = [0,100): 2 indications (ok).  Cycle 1 = [100,200): 2 indications (ok).
  // Evaluate() is never called in between, the supervision must still split them correctly.
  auto s = MakeAlive("A", 2);
  s->Activate(T(0));
  s->OnCheckpoint(CP("E", 1), T(10));
  s->OnCheckpoint(CP("E", 1), T(90));
  s->OnCheckpoint(CP("E", 1), T(105)); // belongs to cycle 1
  s->OnCheckpoint(CP("E", 1), T(190));
  s->Evaluate(T(200));
  EXPECT_EQ(s->Status(), S::kOK);
  EXPECT_EQ(static_cast<AliveSupervision &>(*s).FailedCycles(), 0U);
}

TEST(AliveSupervision, OtherCheckpointsAreNotCounted) {
  auto s = MakeAlive("A", 1);
  s->Activate(T(0));
  s->OnCheckpoint(CP("E", 99), T(10));
  s->OnCheckpoint(CP("Other", 1), T(20));
  s->Evaluate(T(100));
  EXPECT_EQ(s->Status(), S::kEXPIRED);
}

// ---------------------------------------------------------------------------
// Deadline supervision
// ---------------------------------------------------------------------------
namespace {
std::shared_ptr<DeadlineSupervision> MakeDeadline(std::int64_t minMs = 10, std::int64_t maxMs = 50) {
  DeadlineSupervision::Config c;
  c.start = CP("E", 1);
  c.end = CP("E", 2);
  c.minDeadline = Ms(minMs);
  c.maxDeadline = Ms(maxMs);
  return std::make_shared<DeadlineSupervision>("Dl", c);
}
} // namespace

TEST(DeadlineSupervision, WithinBoundsIsOk) {
  auto s = MakeDeadline();
  s->Activate(T(0));
  for (int i = 0; i < 3; ++i) {
    s->OnCheckpoint(CP("E", 1), T(i * 100));
    s->OnCheckpoint(CP("E", 2), T(i * 100 + 30));
  }
  s->OnCheckpoint(CP("E", 1), T(300));
  s->OnCheckpoint(CP("E", 2), T(310)); // exactly min
  s->OnCheckpoint(CP("E", 1), T(400));
  s->OnCheckpoint(CP("E", 2), T(450)); // exactly max
  s->Evaluate(T(500));
  EXPECT_EQ(s->Status(), S::kOK);
}

TEST(DeadlineSupervision, TooFastTooSlowAndTimeout) {
  { auto s = MakeDeadline(); s->Activate(T(0));
    s->OnCheckpoint(CP("E", 1), T(0)); s->OnCheckpoint(CP("E", 2), T(5));
    EXPECT_EQ(s->Status(), S::kEXPIRED); }
  { auto s = MakeDeadline(); s->Activate(T(0));
    s->OnCheckpoint(CP("E", 1), T(0)); s->OnCheckpoint(CP("E", 2), T(51));
    EXPECT_EQ(s->Status(), S::kEXPIRED); }
  { auto s = MakeDeadline(); s->Activate(T(0));
    s->OnCheckpoint(CP("E", 1), T(0));
    s->Evaluate(T(50));
    EXPECT_EQ(s->Status(), S::kOK); // not yet late
    s->Evaluate(T(51));
    EXPECT_EQ(s->Status(), S::kEXPIRED); }
}

TEST(DeadlineSupervision, EndWithoutStartIsIgnoredAndRestartResets) {
  auto s = MakeDeadline();
  s->Activate(T(0));
  s->OnCheckpoint(CP("E", 2), T(5));
  EXPECT_EQ(s->Status(), S::kOK);
  s->OnCheckpoint(CP("E", 1), T(100));
  s->OnCheckpoint(CP("E", 1), T(200)); // restart: measure from here
  s->OnCheckpoint(CP("E", 2), T(230));
  s->Evaluate(T(300));
  EXPECT_EQ(s->Status(), S::kOK);
}

// ---------------------------------------------------------------------------
// Logical supervision
// ---------------------------------------------------------------------------
namespace {
std::shared_ptr<LogicalSupervision> MakeLogical() {
  LogicalSupervision::Config c;
  c.entity = "E";
  c.checkpoints = {1, 2, 3};
  c.initial = {1};
  c.final = {3};
  c.transitions = {{1, 2}, {2, 2}, {2, 3}};
  return std::make_shared<LogicalSupervision>("Log", c);
}
} // namespace

TEST(LogicalSupervision, ValidSequencesRepeat) {
  auto s = MakeLogical();
  s->Activate(T(0));
  for (int run = 0; run < 3; ++run) {
    for (CheckpointType c : {1U, 2U, 2U, 2U, 3U}) {
      s->OnCheckpoint(CP("E", c), T(run));
    }
  }
  EXPECT_EQ(s->Status(), S::kOK);
}

TEST(LogicalSupervision, WrongStartTransitionAndSkippedStepExpire) {
  { auto s = MakeLogical(); s->Activate(T(0));
    s->OnCheckpoint(CP("E", 2), T(0)); // not an initial checkpoint
    EXPECT_EQ(s->Status(), S::kEXPIRED); }
  { auto s = MakeLogical(); s->Activate(T(0));
    s->OnCheckpoint(CP("E", 1), T(0)); s->OnCheckpoint(CP("E", 3), T(1)); // 1->3 not allowed
    EXPECT_EQ(s->Status(), S::kEXPIRED); }
  { auto s = MakeLogical(); s->Activate(T(0));
    for (CheckpointType c : {1U, 2U, 3U}) s->OnCheckpoint(CP("E", c), T(0));
    s->OnCheckpoint(CP("E", 2), T(1)); // after a final checkpoint a new run must start with 1
    EXPECT_EQ(s->Status(), S::kEXPIRED); }
}

TEST(LogicalSupervision, ForeignCheckpointsAreIgnored) {
  auto s = MakeLogical();
  s->Activate(T(0));
  s->OnCheckpoint(CP("E", 1), T(0));
  s->OnCheckpoint(CP("E", 42), T(1));
  s->OnCheckpoint(CP("Other", 3), T(2));
  s->OnCheckpoint(CP("E", 2), T(3));
  EXPECT_EQ(s->Status(), S::kOK);
}

TEST(Supervision, EnumToString) {
  EXPECT_STREQ(ToString(TypeOfSupervision::kDeadlineSupervision), "deadline");
  EXPECT_STREQ(ToString(S::kFAILED), "FAILED");
  EXPECT_STREQ(ToString(GlobalSupervisionStatus::kSTOPPED), "STOPPED");
}
