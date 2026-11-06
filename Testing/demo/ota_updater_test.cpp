#include "ota_updater.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <sstream>

using ap::demo::ExitCode;
using ap::demo::Outcome;
using ap::demo::RunUpdate;
using ap::demo::UpdateOptions;

namespace {
UpdateOptions Quiet() {
  UpdateOptions o;
  o.quiet = true;
  return o;
}
bool Contains(std::vector<std::string> const &v, std::string const &s) {
  for (auto const &x : v) if (x == s) return true;
  return false;
}
} // namespace

TEST(OtaUpdater, HappyPathUpdatesAndReturnsToIdle) {
  std::ostringstream out;
  auto r = RunUpdate(Quiet(), out);
  EXPECT_EQ(r.outcome, Outcome::kUpdated);
  EXPECT_EQ(ExitCode(r), 0);
  EXPECT_EQ(r.ucmStatus, "kIdle");
  EXPECT_EQ(r.machineState, "Driving");
  EXPECT_EQ(r.blocks, 5u); // 5000 bytes / 1024
  for (char const *s : {"kProcessing", "kReady", "kActivating", "kActivated", "kVerifying", "kCleaningUp"})
    EXPECT_TRUE(Contains(r.statusTrail, s)) << s;
  EXPECT_TRUE(Contains(r.machineTrail, "PrepareUpdate"));
  EXPECT_TRUE(Contains(r.machineTrail, "VerifyUpdate"));
}

TEST(OtaUpdater, TamperedPackageIsRejectedAtTransferExit) {
  auto o = Quiet();
  o.tamper = true;
  std::ostringstream out;
  auto r = RunUpdate(o, out);
  EXPECT_EQ(r.outcome, Outcome::kPackageRejected);
  EXPECT_EQ(ExitCode(r), 3);
  EXPECT_NE(r.ucmError, 0);
  EXPECT_EQ(r.ucmStatus, "kIdle");
  EXPECT_FALSE(Contains(r.statusTrail, "kProcessing"));
}

TEST(OtaUpdater, RejectedSessionRevertsAndKeepsMachineRunning) {
  auto o = Quiet();
  o.rejectSession = true;
  std::ostringstream out;
  auto r = RunUpdate(o, out);
  EXPECT_EQ(r.outcome, Outcome::kSessionRejected);
  EXPECT_EQ(ExitCode(r), 4);
  EXPECT_EQ(r.machineState, "Driving");
}

TEST(OtaUpdater, FailedVerificationRollsBack) {
  auto o = Quiet();
  o.failVerify = true;
  std::ostringstream out;
  auto r = RunUpdate(o, out);
  EXPECT_EQ(r.outcome, Outcome::kRolledBack);
  EXPECT_EQ(ExitCode(r), 2);
  EXPECT_TRUE(Contains(r.statusTrail, "kRollingBack"));
  EXPECT_TRUE(Contains(r.statusTrail, "kRolledBack"));
  EXPECT_TRUE(Contains(r.machineTrail, "PrepareRollback"));
  EXPECT_EQ(r.machineState, "Driving");
}

TEST(OtaUpdater, StateFileIsWrittenOnReset) {
  auto o = Quiet();
  o.stateFile = "ota_updater_test.state";
  std::remove(o.stateFile.c_str());
  std::ostringstream out;
  auto r = RunUpdate(o, out);
  EXPECT_EQ(r.outcome, Outcome::kUpdated);
  FILE *f = std::fopen(o.stateFile.c_str(), "r");
  EXPECT_NE(f, nullptr);
  if (f) std::fclose(f);
  std::remove(o.stateFile.c_str());
}

TEST(OtaUpdater, ProgressIsPrintedUnlessQuiet) {
  std::ostringstream loud, quiet;
  UpdateOptions o;
  RunUpdate(o, loud);
  EXPECT_NE(loud.str().find("transfer"), std::string::npos);
  EXPECT_NE(loud.str().find("kActivated"), std::string::npos);
  RunUpdate(Quiet(), quiet);
  EXPECT_TRUE(quiet.str().empty());
}

TEST(OtaUpdater, EmptyPackageFileIsAnError) {
  auto o = Quiet();
  o.packageFile = "/nonexistent/package.bin";
  std::ostringstream out;
  auto r = RunUpdate(o, out);
  EXPECT_EQ(r.outcome, Outcome::kError);
  EXPECT_EQ(ExitCode(r), 1);
}

TEST(OtaUpdater, ToStringNamesOutcomes) {
  EXPECT_STREQ(ap::demo::ToString(Outcome::kRolledBack), "rolled-back");
}
