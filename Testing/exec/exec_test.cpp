#include <gtest/gtest.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "ara/exec/exec_error_domain.h"
#include "ara/exec/execution_client.h"
#include "ara/exec/function_group.h"
#include "ara/exec/function_group_state.h"
#include "ara/exec/state_client.h"

namespace fs = std::filesystem;
using namespace ara::exec;

namespace {

class ExecTest : public ::testing::Test {
protected:
  void SetUp() override {
    old_ = fs::current_path();
    dir_ = fs::temp_directory_path() / ("exec_ut_" + std::to_string(::getpid()));
    fs::create_directories(dir_);
    fs::current_path(dir_);
  }
  void TearDown() override {
    fs::current_path(old_);
    fs::remove_all(dir_);
  }
  std::string WriteManifest(std::string const &text) {
    auto p = dir_ / "machine_manifest.json";
    std::ofstream(p) << text;
    return p.string();
  }
  static constexpr char const *kManifest = R"({
    "Machine_manifest": { "Mode_declaration_group": [
      { "Function_group_name": "MachineFG",
        "Mode_declarations": [ {"Mode":"Startup"}, {"Mode":"Running"}, {"Mode":"Shutdown"} ] },
      { "Function_group_name": "FG_1", "Mode_declarations": [ {"Mode":"Off"}, {"Mode":"On"} ] }
    ]}})";
  fs::path old_, dir_;
};

TEST_F(ExecTest, ErrorDomainMessagesAreStable) {
  auto ec = MakeErrorCode(ExecErrc::kCancelled);
  EXPECT_STREQ(ec.Domain().Name(), "Exec");
  EXPECT_NE(std::string(ec.Message()), "");
  EXPECT_THROW(ec.ThrowAsException(), ExecException);
}

TEST_F(ExecTest, PreconstructFindsFunctionGroupAndStates) {
  auto m = WriteManifest(kManifest);
  auto r = FunctionGroup::Preconstruct(m + "/MachineFG");
  ASSERT_EQ(r.index(), 1U);
  FunctionGroup fg(std::move(std::get<1>(r)));
  EXPECT_EQ(fg.get_FGname(), "MachineFG");
  EXPECT_EQ(fg.get_states(), (std::vector<std::string>{"Startup", "Running", "Shutdown"}));
}

TEST_F(ExecTest, PreconstructRejectsBadInputWithoutTerminating) {
  auto m = WriteManifest(kManifest);
  auto expectMeta = [](auto const &r) {
    ASSERT_EQ(r.index(), 0U);
    EXPECT_EQ(std::get<0>(r), ExecErrc::kMetaModelError);
  };
  expectMeta(FunctionGroup::Preconstruct(m + "/Unknown"));
  expectMeta(FunctionGroup::Preconstruct((dir_ / "missing.json").string() + "/FG_1"));
  expectMeta(FunctionGroup::Preconstruct("no_slash_at_all"));
  expectMeta(FunctionGroup::Preconstruct(m + "/"));
  auto bad = WriteManifest("{ not json");
  expectMeta(FunctionGroup::Preconstruct(bad + "/FG_1"));
  WriteManifest(R"({"Machine_manifest": 5})");
  expectMeta(FunctionGroup::Preconstruct(bad + "/FG_1"));
}

TEST_F(ExecTest, FunctionGroupStateChecksGroupAndComparesState) {
  auto m = WriteManifest(kManifest);
  auto r = FunctionGroup::Preconstruct(m + "/FG_1");
  FunctionGroup fg(std::move(std::get<1>(r)));

  auto on = FunctionGroupState::Preconstruct(fg, "FG_1/On");
  auto off = FunctionGroupState::Preconstruct(fg, "FG_1/Off");
  ASSERT_EQ(on.index(), 1U);
  ASSERT_EQ(off.index(), 1U);
  FunctionGroupState son(std::move(std::get<1>(on)));
  FunctionGroupState soff(std::move(std::get<1>(off)));
  EXPECT_EQ(son.get_states(), "On");
  EXPECT_TRUE(son != soff); // different state of the same group
  EXPECT_FALSE(son == soff);

  EXPECT_EQ(std::get<0>(FunctionGroupState::Preconstruct(fg, "Other/On")), ExecErrc::kMetaModelError);
  EXPECT_EQ(std::get<0>(FunctionGroupState::Preconstruct(fg, "FG_1")), ExecErrc::kMetaModelError);
  EXPECT_EQ(std::get<0>(FunctionGroupState::Preconstruct(fg, "FG_1/")), ExecErrc::kMetaModelError);
}

TEST_F(ExecTest, ClientsReportCommunicationErrorWithoutPeer) {
  ExecutionClient ec; // no FIFO exists -> open fails, must not crash
  auto r = ec.ReportExecutionState(ExecutionState::kRunning);
  ASSERT_TRUE(std::holds_alternative<ExecErrc>(r));
  EXPECT_EQ(std::get<ExecErrc>(r), ExecErrc::kCommunicationError);
}

TEST_F(ExecTest, StateClientWithoutPeerFailsAndInitialResultIsReady) {
  StateClient sc;
  auto r = sc.SetState([&] {
    auto m = WriteManifest(kManifest);
    auto fg = FunctionGroup(std::move(std::get<1>(FunctionGroup::Preconstruct(m + "/FG_1"))));
    return FunctionGroupState(std::move(std::get<1>(FunctionGroupState::Preconstruct(fg, "FG_1/On"))));
  }()); // temporary state dies right after the call: SetState must not dangle
  auto v = r.get();
  ASSERT_TRUE(std::holds_alternative<ExecErrc>(v));
  EXPECT_EQ(std::get<ExecErrc>(v), ExecErrc::kCommunicationError);

  auto init = sc.GetInitialMachineStateTransitionResult();
  ASSERT_TRUE(init.valid());
  EXPECT_TRUE(std::holds_alternative<std::monostate>(init.get()));
}

TEST_F(ExecTest, StateClientDeliversRequestOverFifo) {
  ASSERT_EQ(::mkfifo("state_client_fifo", 0600), 0);
  int rd = ::open("state_client_fifo", O_RDONLY | O_NONBLOCK);
  ASSERT_GE(rd, 0);
  StateClient sc;
  auto m = WriteManifest(kManifest);
  FunctionGroup fg(std::move(std::get<1>(FunctionGroup::Preconstruct(m + "/FG_1"))));
  FunctionGroupState st(std::move(std::get<1>(FunctionGroupState::Preconstruct(fg, "FG_1/On"))));
  auto v = sc.SetState(st).get();
  EXPECT_TRUE(std::holds_alternative<std::monostate>(v));
  char buf[64] = {};
  ssize_t n = ::read(rd, buf, sizeof buf - 1);
  ::close(rd);
  ASSERT_GT(n, 0);
  EXPECT_EQ(std::string(buf, static_cast<size_t>(n)), "FG_1/On\n");
}

} // namespace
