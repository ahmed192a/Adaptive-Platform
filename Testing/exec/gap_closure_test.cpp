// Tests for the spec-shaped ExecutionClient::Create / StateClient error APIs and the EM->SM execution error
// protocol (SWS_EM_02577, SWS_EM_02582, SWS_EM_01309, SWS_EM_02543). Signatures not readable in the SWS text are
// flagged Unverified in doc/spec-conformance/exec.md.
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <mutex>
#include <thread>

#include "ara/exec/exec_error_domain.h"
#include "ara/exec/execution_client.h"
#include "ara/exec/execution_error_wire.h"
#include "ara/exec/state_client.h"

namespace fs = std::filesystem;
using namespace ara::exec;
using namespace std::chrono_literals;

namespace {
struct GapExec : ::testing::Test {
  void SetUp() override {
    old_ = fs::current_path();
    dir_ = fs::temp_directory_path() / ("exec_gap_" + std::to_string(::getpid()));
    fs::create_directories(dir_);
    fs::current_path(dir_);
  }
  void TearDown() override {
    fs::current_path(old_);
    fs::remove_all(dir_);
  }
  template <typename Pred> static bool WaitFor(Pred p, std::chrono::milliseconds limit = 3000ms) {
    auto const end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end) {
      if (p()) return true;
      std::this_thread::sleep_for(10ms);
    }
    return p();
  }
  fs::path old_, dir_;
};
} // namespace

// ---- SWS_EM_02582 / SWS_EM_02577 -------------------------------------------------------------------
TEST_F(GapExec, CreateEnforcesSingleInstanceAndReleasesTheSlot) {
  auto first = ExecutionClient::Create([] {});
  ASSERT_TRUE(first.HasValue());
  auto second = ExecutionClient::Create([] {});
  ASSERT_FALSE(second.HasValue());
  EXPECT_EQ(second.Error(), MakeErrorCode(ExecErrc::kGeneralError));
  {
    ExecutionClient moved = std::move(first).Value(); // moving keeps the slot owned by one object
    EXPECT_FALSE(ExecutionClient::Create([] {}).HasValue());
  } // destroyed => slot free again
  EXPECT_TRUE(ExecutionClient::Create([] {}).HasValue());
}

TEST_F(GapExec, CreateRejectsAnEmptyTerminationHandler) {
  auto r = ExecutionClient::Create(nullptr);
  ASSERT_FALSE(r.HasValue());
  EXPECT_EQ(r.Error(), MakeErrorCode(ExecErrc::kInvalidArguments));
}

TEST_F(GapExec, LegacyConstructorRemainsUnrestricted) {
  ExecutionClient a;
  ExecutionClient b; // several platform processes rely on this
  auto created = ExecutionClient::Create([] {});
  EXPECT_TRUE(created.HasValue());
}

TEST_F(GapExec, SigtermRunsTheTerminationHandlerOnABackgroundThread) {
  std::atomic<bool> called{false};
  std::thread::id handlerThread;
  auto client = ExecutionClient::Create([&] {
    handlerThread = std::this_thread::get_id();
    called = true;
  });
  ASSERT_TRUE(client.HasValue());
  ASSERT_EQ(::kill(::getpid(), SIGTERM), 0);
  ASSERT_TRUE(WaitFor([&] { return called.load(); }));
  EXPECT_NE(handlerThread, std::this_thread::get_id());
}

TEST_F(GapExec, DestroyingTheClientDoesNotInvokeTheHandler) {
  std::atomic<int> calls{0};
  {
    auto client = ExecutionClient::Create([&] { ++calls; });
    ASSERT_TRUE(client.HasValue());
  }
  std::this_thread::sleep_for(50ms);
  EXPECT_EQ(calls.load(), 0);
}

// ---- ReportExecutionStateResult ----------------------------------------------------------------------
TEST_F(GapExec, ReportExecutionStateResultMirrorsTheLegacyApi) {
  ASSERT_EQ(::mkfifo("execution_client_fifo", 0600), 0);
  int reader = ::open("execution_client_fifo", O_RDONLY | O_NONBLOCK);
  ASSERT_GE(reader, 0);
  {
    ExecutionClient ec;
    EXPECT_TRUE(ec.ReportExecutionStateResult(ExecutionState::kRunning).HasValue());
    auto again = ec.ReportExecutionStateResult(ExecutionState::kRunning); // SWS_EM_02243
    ASSERT_FALSE(again.HasValue());
    EXPECT_EQ(again.Error(), MakeErrorCode(ExecErrc::kInvalidTransition));
    EXPECT_TRUE(ec.ReportExecutionStateResult(ExecutionState::kTerminating).HasValue());
  }
  ::close(reader);
}

TEST_F(GapExec, ReportExecutionStateResultReportsCommunicationErrors) {
  ExecutionClient ec; // no FIFO
  auto r = ec.ReportExecutionStateResult(ExecutionState::kRunning);
  ASSERT_FALSE(r.HasValue());
  EXPECT_EQ(r.Error(), MakeErrorCode(ExecErrc::kCommunicationError));
}

// ---- execution error wire format (SWS_EM_01309 / SWS_EM_02543) -----------------------------------
TEST(ExecErrorWire, FormatAndParseRoundTrip) {
  EXPECT_EQ(FormatExecutionErrorLine("FG_1", 7), "FG_1/7\n");
  auto p = ParseExecutionErrorLine("FG_1/7");
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->first, "FG_1");
  EXPECT_EQ(p->second, 7u);
}

TEST(ExecErrorWire, RejectsMalformedLines) {
  for (char const *bad : {"", "FG_1", "/3", "FG_1/", "FG_1/x", "FG_1/-1", "FG_1/99999999999999999999"}) {
    EXPECT_FALSE(ParseExecutionErrorLine(bad).has_value()) << bad;
  }
}

TEST(ExecErrorWire, DefaultExecutionErrorIsOne) { EXPECT_EQ(kDefaultExecutionError, 1u); }

TEST(ExecErrorWire, AbnormalTerminationDecision) {
  EXPECT_FALSE(IsAbnormalTermination(0));              // exit(0)
  EXPECT_TRUE(IsAbnormalTermination(1 << 8));          // exit(1)
  EXPECT_TRUE(IsAbnormalTermination(SIGSEGV));         // killed by signal
  EXPECT_TRUE(IsAbnormalTermination(SIGKILL));
}

// ---- StateClient: undefinedStateCallback / GetExecutionError --------------------------------------
TEST_F(GapExec, StateClientDeliversExecutionErrorsToCallbackAndGetExecutionError) {
  ASSERT_EQ(::mkfifo("execution_error_fifo", 0600), 0);
  int writer = ::open("execution_error_fifo", O_RDWR | O_NONBLOCK); // EM keeps this end open
  ASSERT_GE(writer, 0);
  std::mutex m;
  std::vector<std::pair<std::string, ExecutionError>> got;
  StateClient client([&](ExecutionErrorEvent const &ev) {
    std::lock_guard<std::mutex> lock(m);
    got.emplace_back(ev.functionGroup.get_FGname(), ev.executionError);
  });
  // two events glued together + a malformed one + a split one
  std::string const data = FormatExecutionErrorLine("FG_1", 7) + "garbage\n" + FormatExecutionErrorLine("Brake", 1);
  ASSERT_EQ(::write(writer, data.data(), 12), 12); // "FG_1/7\ngarba"
  std::this_thread::sleep_for(120ms);
  ASSERT_EQ(::write(writer, data.data() + 12, data.size() - 12), static_cast<ssize_t>(data.size() - 12));
  ASSERT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(m);
    return got.size() == 2;
  }));
  {
    std::lock_guard<std::mutex> lock(m);
    EXPECT_EQ(got[0], (std::pair<std::string, ExecutionError>{"FG_1", 7}));
    EXPECT_EQ(got[1], (std::pair<std::string, ExecutionError>{"Brake", 1}));
  }
  FunctionGroup::CtorToken t1 = {{"Function_group_name", "FG_1"}, {"Mode_declarations", nlohmann::json::array()}};
  FunctionGroup fg1(std::move(t1));
  auto ev = client.GetExecutionError(fg1);
  ASSERT_TRUE(ev.HasValue());
  EXPECT_EQ(ev.Value().executionError, 7u);
  FunctionGroup::CtorToken t2 = {{"Function_group_name", "Other"}, {"Mode_declarations", nlohmann::json::array()}};
  FunctionGroup other(std::move(t2));
  auto none = client.GetExecutionError(other);
  ASSERT_FALSE(none.HasValue());
  EXPECT_EQ(none.Error(), MakeErrorCode(ExecErrc::kGeneralError));
  ::close(writer);
}

TEST_F(GapExec, StateClientWithoutErrorFifoStartsAndStopsCleanly) {
  std::atomic<int> calls{0};
  {
    StateClient client([&](ExecutionErrorEvent const &) { ++calls; });
    std::this_thread::sleep_for(100ms);
  }
  EXPECT_EQ(calls.load(), 0);
}

// ---- Future variants ---------------------------------------------------------------------------------
TEST_F(GapExec, StateClientFutureVariantsReportErrorsAndSuccess) {
  StateClient client; // no state_client_fifo
  FunctionGroupState::CtorToken token{"FG_1", "on"};
  FunctionGroupState state(std::move(token));
  auto f = client.SetStateFuture(state);
  auto r = f.GetResult();
  ASSERT_FALSE(r.HasValue());
  EXPECT_EQ(r.Error(), MakeErrorCode(ExecErrc::kCommunicationError));
  EXPECT_TRUE(client.GetInitialMachineStateTransitionFuture().GetResult().HasValue());
}
