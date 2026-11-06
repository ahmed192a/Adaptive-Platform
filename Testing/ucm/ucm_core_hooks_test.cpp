// Tests for the State-Management hooks, verification, history and persistence of the UCM core
// (ucm_core.h). Requirement ids refer to AUTOSAR SWS UCM (R22-11 text; see doc/spec-conformance/ucm.md).
#include "ara/ucm/pkgmgr/ucm_core.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <set>
#include <vector>

using namespace ara::ucm::pkgmgr;
using core::PackageManagerCore;
using core::UpdateHooks;
namespace st = core::status;
namespace ps = core::pkgstate;
namespace E = ara::ucm::pkgmgr::PackageManagement;

namespace {

E::TransferIdType MakeId(std::uint8_t seed) {
  E::TransferIdType id{};
  for (std::size_t i = 0; i < id.size(); ++i) id[i] = static_cast<std::uint8_t>(seed + i);
  return id;
}

// Uploads a 2500 byte package (3 blocks of 1024) and returns its id; the core ends in kTransferred.
E::TransferIdType Upload(PackageManagerCore &c, std::uint8_t seed = 1) {
  auto id = MakeId(seed);
  auto s = c.TransferStart(2500, id);
  EXPECT_EQ(s.error, 0);
  EXPECT_EQ(c.TransferData(id, 1024, 0).error, 0);
  EXPECT_EQ(c.TransferData(id, 1024, 1).error, 0);
  EXPECT_EQ(c.TransferData(id, 452, 2).error, 0);
  return id;
}

E::TransferIdType ProcessOne(PackageManagerCore &c, std::uint8_t seed = 1) {
  auto id = Upload(c, seed);
  EXPECT_EQ(c.TransferExit(id), 0);
  EXPECT_EQ(c.BeginProcess(id), 0);
  c.EndProcess(id, true);
  return id;
}

std::string TempPath(char const *name) { return std::string(::testing::TempDir()) + name; }

} // namespace

// ------------------------------------------------------ processing failure
TEST(UcmProcessingFailed, CancelledProcessingMarksThePackageFailed) { // 00003 / 00392
  PackageManagerCore c;
  auto id = Upload(c);
  ASSERT_EQ(c.TransferExit(id), 0);
  ASSERT_EQ(c.BeginProcess(id), 0);
  c.EndProcessFailed(id, E::ProcessSwPackageCancelled);
  EXPECT_EQ(c.PackageState(id), ps::kProcessingFailed);
  EXPECT_EQ(c.Status(), st::kIdle);
}

TEST(UcmProcessingFailed, FailedPackageCanBeRetriedOrDeleted) {
  PackageManagerCore c;
  auto id = Upload(c);
  ASSERT_EQ(c.TransferExit(id), 0);
  ASSERT_EQ(c.BeginProcess(id), 0);
  c.EndProcessFailed(id, E::ProcessedSoftwarePackageInconsistent);
  EXPECT_EQ(c.BeginProcess(id), 0); // retry
  c.EndProcess(id, true);
  EXPECT_EQ(c.PackageState(id), ps::kProcessed);
  auto id2 = Upload(c, 9);
  ASSERT_EQ(c.TransferExit(id2), 0);
  ASSERT_EQ(c.BeginProcess(id2), 0);
  c.EndProcessFailed(id2, 1);
  EXPECT_EQ(c.Status(), st::kReady); // the first package is still processed
  EXPECT_EQ(c.DeleteTransfer(id2), 0);
}

// -------------------------------------------------------------- activation
TEST(UcmActivateHooks, WithoutHooksActivationSucceeds) {
  PackageManagerCore c;
  ProcessOne(c);
  EXPECT_EQ(c.Activate(), 0);
  EXPECT_EQ(c.Status(), st::kActivated);
}

TEST(UcmActivateHooks, RejectedUpdateSessionKeepsReady) { // 00258
  PackageManagerCore c;
  UpdateHooks h;
  h.prepareUpdate = [] { return static_cast<int>(E::UpdateSessionRejected); };
  c.SetHooks(h);
  ProcessOne(c);
  EXPECT_EQ(c.Activate(), E::UpdateSessionRejected);
  EXPECT_EQ(c.Status(), st::kReady);
}

TEST(UcmActivateHooks, PrepareThenResetAreCalledInOrder) {
  PackageManagerCore c;
  std::string order;
  UpdateHooks h;
  h.prepareUpdate = [&] { order += "P"; return 0; };
  h.resetMachine = [&] { order += "R"; return 0; };
  c.SetHooks(h);
  ProcessOne(c);
  EXPECT_EQ(c.Activate(), 0);
  EXPECT_EQ(order, "PR");
}

TEST(UcmActivateHooks, FailingResetKeepsReady) {
  PackageManagerCore c;
  UpdateHooks h;
  h.resetMachine = [] { return static_cast<int>(E::PreActivationFailed); };
  c.SetHooks(h);
  ProcessOne(c);
  EXPECT_EQ(c.Activate(), E::PreActivationFailed);
  EXPECT_EQ(c.Status(), st::kReady);
}

TEST(UcmActivateHooks, ActivateOutsideReadyIsNotPermitted) { // 00241
  PackageManagerCore c;
  EXPECT_EQ(c.Activate(), E::OperationNotPermitted);
}

// ------------------------------------------------------------ verification
TEST(UcmVerify, SuccessReturnsToActivated) {
  PackageManagerCore c;
  std::vector<int> seen;
  c.OnStatusChange([&](E::PackageManagerStatusType s) { seen.push_back(s); });
  ProcessOne(c);
  ASSERT_EQ(c.Activate(), 0);
  EXPECT_EQ(c.Verify(), 0);
  EXPECT_EQ(c.Status(), st::kActivated);
  bool sawVerifying = false;
  for (int s : seen) sawVerifying |= s == st::kVerifying;
  EXPECT_TRUE(sawVerifying);
}

TEST(UcmVerify, FailureStaysVerifyingAndOnlyRollbackOrFinishRemain) { // 00280
  PackageManagerCore c;
  UpdateHooks h;
  h.verifyUpdate = [] { return 1; };
  c.SetHooks(h);
  ProcessOne(c);
  ASSERT_EQ(c.Activate(), 0);
  EXPECT_EQ(c.Verify(), E::VerificationFailed);
  EXPECT_EQ(c.Status(), st::kVerifying);
  EXPECT_EQ(c.Verify(), E::OperationNotPermitted);
  EXPECT_EQ(c.Rollback(), 0); // 00239: allowed from kVerifying
  EXPECT_EQ(c.Status(), st::kRolledBack);
}

TEST(UcmVerify, NotAllowedBeforeActivation) {
  PackageManagerCore c;
  EXPECT_EQ(c.Verify(), E::OperationNotPermitted);
  ProcessOne(c);
  EXPECT_EQ(c.Verify(), E::OperationNotPermitted);
}

// ---------------------------------------------------------------- rollback
TEST(UcmRollbackHooks, PrepareRollbackAndResetRun) { // 00110
  PackageManagerCore c;
  std::string order;
  UpdateHooks h;
  h.prepareRollback = [&] { order += "B"; return 0; };
  h.resetMachine = [&] { order += "R"; return 0; };
  c.SetHooks(h);
  ProcessOne(c);
  ASSERT_EQ(c.Activate(), 0);
  order.clear();
  EXPECT_EQ(c.Rollback(), 0);
  EXPECT_EQ(order, "BR");
  EXPECT_EQ(c.Status(), st::kRolledBack);
}

TEST(UcmRollbackHooks, FailedPrepareReportsNotAbleToRollbackAndKeepsState) {
  PackageManagerCore c;
  UpdateHooks h;
  h.prepareRollback = [] { return 1; };
  c.SetHooks(h);
  ProcessOne(c);
  ASSERT_EQ(c.Activate(), 0);
  EXPECT_EQ(c.Rollback(), E::NotAbleToRollback);
  EXPECT_EQ(c.Status(), st::kActivated);
}

TEST(UcmRollbackHooks, OnlyAfterActivation) { // 00239
  PackageManagerCore c;
  EXPECT_EQ(c.Rollback(), E::OperationNotPermitted);
  EXPECT_EQ(c.Status(), st::kIdle);
}

// ------------------------------------------------------------------ revert
TEST(UcmRevertHooks, FailingRevertKeepsPackagesProcessed) { // 00236
  PackageManagerCore c;
  UpdateHooks h;
  h.revertPackages = [] { return 1; };
  c.SetHooks(h);
  auto id = ProcessOne(c);
  EXPECT_EQ(c.Revert(), E::NotAbleToRevertPackages);
  EXPECT_EQ(c.PackageState(id), ps::kProcessed);
  EXPECT_EQ(c.Status(), st::kReady);
}

TEST(UcmRevertHooks, SuccessfulRevertGoesIdle) { // 00237
  PackageManagerCore c;
  auto id = ProcessOne(c);
  EXPECT_EQ(c.Revert(), 0);
  EXPECT_EQ(c.PackageState(id), ps::kTransferred);
  EXPECT_EQ(c.Status(), st::kIdle);
}

TEST(UcmRevertHooks, NotAllowedAfterActivation) {
  PackageManagerCore c;
  ProcessOne(c);
  ASSERT_EQ(c.Activate(), 0);
  EXPECT_EQ(c.Revert(), E::OperationNotPermitted);
}

// -------------------------------------------------------------- validation
TEST(UcmValidation, FailedValidationKeepsTheTransferOpen) { // 00098 / 00092 / 00103
  PackageManagerCore c;
  bool good = false;
  UpdateHooks h;
  h.validatePackage = [&](E::TransferIdType const &) {
    return good ? 0 : static_cast<int>(E::AuthenticationFailed);
  };
  c.SetHooks(h);
  auto id = Upload(c);
  EXPECT_EQ(c.TransferExit(id), E::AuthenticationFailed);
  EXPECT_EQ(c.PackageState(id), ps::kTransferring);
  good = true;
  EXPECT_EQ(c.TransferExit(id), 0);
  EXPECT_EQ(c.PackageState(id), ps::kTransferred);
}

TEST(UcmValidation, InsufficientDataIsReportedBeforeValidation) { // 00087
  PackageManagerCore c;
  bool called = false;
  UpdateHooks h;
  h.validatePackage = [&](E::TransferIdType const &) { called = true; return 0; };
  c.SetHooks(h);
  auto id = MakeId(3);
  ASSERT_EQ(c.TransferStart(2000, id).error, 0);
  ASSERT_EQ(c.TransferData(id, 1024, 0).error, 0);
  EXPECT_EQ(c.TransferExit(id), E::InsufficientData);
  EXPECT_FALSE(called);
}

TEST(UcmValidation, NewErrorCodesAreDistinct) {
  std::set<int> v{E::TransferFailed, E::PackageFormatUnsupported, E::SwclRemovalDenied,
                  E::PersistencyAllocationFailed, E::NotAbleToRevertPackages, E::UpdateSessionRejected};
  EXPECT_EQ(v.size(), 6u);
}

// ----------------------------------------------------------- history & id
TEST(UcmHistory, RecordsOperationsInOrderWithTheInjectedClock) {
  PackageManagerCore c;
  std::uint64_t now = 100;
  c.SetClock([&] { return now; });
  ProcessOne(c);
  now = 105;
  ASSERT_EQ(c.Activate(), 0);
  now = 110;
  ASSERT_EQ(c.Rollback(), 0);
  auto h = c.History();
  ASSERT_EQ(h.size(), 2u);
  EXPECT_EQ(h[0].action, "Activate");
  EXPECT_EQ(h[0].timeSeconds, 105u);
  EXPECT_EQ(h[0].sequence, 1u);
  EXPECT_EQ(h[1].action, "Rollback");
  EXPECT_EQ(h[1].sequence, 2u);
  EXPECT_EQ(h[1].result, 0);
}

TEST(UcmHistory, FailuresAreRecordedWithTheirError) {
  PackageManagerCore c;
  EXPECT_EQ(c.Activate(), E::OperationNotPermitted);
  auto h = c.History();
  ASSERT_EQ(h.size(), 1u);
  EXPECT_EQ(h[0].result, E::OperationNotPermitted);
}

TEST(UcmHistory, IdDefaultsAndCanBeSet) {
  PackageManagerCore c;
  EXPECT_EQ(c.GetId(), "UCM");
  c.SetId("ucm-body-ecu");
  EXPECT_EQ(c.GetId(), "ucm-body-ecu");
}

// -------------------------------------------------------------- persistence
TEST(UcmPersistence, RoundTripKeepsStatusAndPackages) { // 00184
  auto path = TempPath("ucm_state_rt.txt");
  PackageManagerCore a;
  auto done = ProcessOne(a, 1);
  auto half = MakeId(7);
  ASSERT_EQ(a.TransferStart(3000, half).error, 0);
  ASSERT_EQ(a.TransferData(half, 1024, 0).error, 0);
  ASSERT_TRUE(a.Save(path));

  PackageManagerCore b;
  ASSERT_TRUE(b.Load(path));
  EXPECT_EQ(b.Status(), st::kReady);
  EXPECT_EQ(b.PackageState(done), ps::kProcessed);
  EXPECT_EQ(b.PackageState(half), ps::kTransferring);
  // the half-finished upload can be resumed at the right block
  EXPECT_EQ(b.TransferData(half, 1024, 0).error, E::IncorrectBlock);
  EXPECT_EQ(b.TransferData(half, 1024, 1).error, 0);
  EXPECT_EQ(b.TransferData(half, 952, 2).error, 0);
  EXPECT_EQ(b.TransferExit(half), 0);
  std::remove(path.c_str());
}

TEST(UcmPersistence, InFlightStatesResolveConservatively) {
  auto path = TempPath("ucm_state_flight.txt");
  struct Case { unsigned saved; unsigned expected; };
  for (Case cs : {Case{st::kActivating, st::kActivated}, Case{st::kRollingBack, st::kRolledBack},
                  Case{st::kCleaningUp, st::kIdle}, Case{st::kProcessing, st::kIdle},
                  Case{st::kVerifying, st::kVerifying}}) {
    {
      std::ofstream f(path);
      f << "ucm-state 1\nstatus " << cs.saved << "\n";
    }
    PackageManagerCore c;
    ASSERT_TRUE(c.Load(path)) << cs.saved;
    EXPECT_EQ(c.Status(), cs.expected) << cs.saved;
  }
  std::remove(path.c_str());
}

TEST(UcmPersistence, ProcessingPackageBecomesFailedAfterReboot) {
  auto path = TempPath("ucm_state_proc.txt");
  PackageManagerCore a;
  auto id = Upload(a);
  ASSERT_EQ(a.TransferExit(id), 0);
  ASSERT_EQ(a.BeginProcess(id), 0);
  ASSERT_TRUE(a.Save(path)); // saved in the middle of ProcessSwPackage
  PackageManagerCore b;
  ASSERT_TRUE(b.Load(path));
  EXPECT_EQ(b.PackageState(id), ps::kProcessingFailed);
  EXPECT_EQ(b.Status(), st::kIdle);
  std::remove(path.c_str());
}

TEST(UcmPersistence, MalformedOrMissingFileLeavesStateUntouched) {
  auto path = TempPath("ucm_state_bad.txt");
  PackageManagerCore c;
  ProcessOne(c);
  EXPECT_FALSE(c.Load(TempPath("does_not_exist.txt")));
  for (char const *bad : {"", "garbage\n", "ucm-state 2\nstatus 0\n", "ucm-state 1\nstatus 99\n",
                          "ucm-state 1\nstatus 0\npkg zz 1 1 1 1\n",
                          "ucm-state 1\nstatus 0\npkg 000102030405060708090a0b0c0d0e0f 1 1 1 77\n"}) {
    { std::ofstream f(path); f << bad; }
    EXPECT_FALSE(c.Load(path)) << bad;
    EXPECT_EQ(c.Status(), st::kReady) << bad;
  }
  std::remove(path.c_str());
}

TEST(UcmPersistence, SaveToUnwritableDirectoryFails) {
  PackageManagerCore c;
  EXPECT_FALSE(c.Save("/nonexistent-dir-for-ucm/state.txt"));
}

TEST(UcmPersistence, StatusChangeNotificationFiresOnLoad) {
  auto path = TempPath("ucm_state_notify.txt");
  PackageManagerCore a;
  ProcessOne(a);
  ASSERT_TRUE(a.Save(path));
  PackageManagerCore b;
  int last = -1;
  b.OnStatusChange([&](E::PackageManagerStatusType s) { last = s; });
  ASSERT_TRUE(b.Load(path));
  EXPECT_EQ(last, st::kReady);
  std::remove(path.c_str());
}
