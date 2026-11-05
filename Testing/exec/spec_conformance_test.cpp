// Pins behaviour required by AUTOSAR AP R25-11 SWS ExecutionManagement
// (requirement ids as quoted in doc/spec-conformance/exec.md).
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>

#include "ara/exec/exec_error_domain.h"
#include "ara/exec/execution_client.h"

namespace fs = std::filesystem;
using namespace ara::exec;

namespace {
struct SpecExec : ::testing::Test {
  void SetUp() override {
    old_ = fs::current_path();
    dir_ = fs::temp_directory_path() / ("exec_spec_" + std::to_string(::getpid()));
    fs::create_directories(dir_);
    fs::current_path(dir_);
  }
  void TearDown() override {
    fs::current_path(old_);
    fs::remove_all(dir_);
  }
  fs::path old_, dir_;
};
} // namespace

// SWS_EM_02243: ReportExecutionState(kRunning) is only valid from the Starting state.
TEST_F(SpecExec, SecondKRunningReportIsAnInvalidTransition) {
  ASSERT_EQ(::mkfifo("execution_client_fifo", 0600), 0);
  int reader = ::open("execution_client_fifo", O_RDONLY | O_NONBLOCK);
  ASSERT_GE(reader, 0);
  {
    ExecutionClient ec;
    auto first = ec.ReportExecutionState(ExecutionState::kRunning);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(first));
    auto second = ec.ReportExecutionState(ExecutionState::kRunning);
    ASSERT_TRUE(std::holds_alternative<ExecErrc>(second));
    EXPECT_EQ(std::get<ExecErrc>(second), ExecErrc::kInvalidTransition);
    // only the first report reached Execution Management
    ExecutionState got{};
    EXPECT_EQ(::read(reader, &got, sizeof(got)), static_cast<ssize_t>(sizeof(got)));
    EXPECT_EQ(got, ExecutionState::kRunning);
    EXPECT_EQ(::read(reader, &got, sizeof(got)), -1);
    // terminating is still allowed afterwards
    EXPECT_TRUE(std::holds_alternative<std::monostate>(ec.ReportExecutionState(ExecutionState::kTerminating)));
  }
  ::close(reader);
}

// A failed transport must not consume the Starting state.
TEST_F(SpecExec, FailedKRunningReportCanBeRetried) {
  ExecutionClient ec; // no FIFO yet
  auto r = ec.ReportExecutionState(ExecutionState::kRunning);
  ASSERT_TRUE(std::holds_alternative<ExecErrc>(r));
  EXPECT_EQ(std::get<ExecErrc>(r), ExecErrc::kCommunicationError);
  auto again = ec.ReportExecutionState(ExecutionState::kRunning);
  EXPECT_EQ(std::get<ExecErrc>(again), ExecErrc::kCommunicationError); // not kInvalidTransition
}

// SWS_EM_02284/02290: the domain accessor has the specified name.
TEST(SpecExecDomain, GetExecErrorDomainMatchesLegacyAccessor) {
  EXPECT_EQ(&GetExecErrorDomain(), &GetExecDomain());
  EXPECT_STREQ(GetExecErrorDomain().Name(), "Exec");
}

// Guards against accidental renumbering of ExecErrc (numeric values not verified against SWS chapter 8).
TEST(SpecExecDomain, ErrorCodeNumbersAreStable) {
  EXPECT_EQ(static_cast<int>(ExecErrc::kGeneralError), 1);
  EXPECT_EQ(static_cast<int>(ExecErrc::kInvalidTransition), 9);
}
