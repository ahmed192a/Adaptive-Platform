// Unit tests for the UCM PackageManagement rules (ucm_core.h): CurrentStatus state
// machine and transfer bookkeeping. Requirement ids refer to AUTOSAR SWS UCM.
#include "ara/ucm/pkgmgr/ucm_core.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace ara::ucm::pkgmgr;
using core::PackageManagerCore;
namespace st = core::status;
namespace ps = core::pkgstate;
namespace E = ara::ucm::pkgmgr::PackageManagement;

namespace {

PackageManagement::TransferIdType MakeId(std::uint8_t seed) {
  PackageManagement::TransferIdType id{};
  for (std::size_t i = 0; i < id.size(); ++i) id[i] = static_cast<std::uint8_t>(seed + i);
  return id;
}

// Transfers `size` bytes in blocks of 1024 through the core; returns the id.
PackageManagement::TransferIdType Upload(PackageManagerCore &c, std::uint64_t size,
                                         std::uint8_t seed = 1, bool padLast = false) {
  auto id = MakeId(seed);
  auto s = c.TransferStart(size, id);
  EXPECT_EQ(s.error, 0);
  std::uint64_t sent = 0, block = 0;
  while (sent < size) {
    std::uint64_t n = std::min<std::uint64_t>(s.blockSize, size - sent);
    auto r = c.TransferData(id, padLast ? s.blockSize : n, block++);
    EXPECT_EQ(r.error, 0);
    EXPECT_EQ(r.accepted, n);
    sent += n;
  }
  EXPECT_EQ(c.TransferExit(id), 0);
  return id;
}

} // namespace

TEST(UcmCore, StartsIdle) {
  PackageManagerCore c;
  EXPECT_EQ(c.Status(), st::kIdle);
}

TEST(UcmCore, IdToStringIs32HexDigits) {
  EXPECT_EQ(core::IdToString(MakeId(0)), "000102030405060708090a0b0c0d0e0f");
  EXPECT_NE(core::IdToString(MakeId(1)), core::IdToString(MakeId(2)));
}

// ---------------------------------------------------------------- transfer
TEST(UcmTransfer, StartReturnsBlockSizeAndTracksPackage) {
  PackageManagerCore c;
  auto id = MakeId(7);
  auto r = c.TransferStart(5000, id);
  EXPECT_EQ(r.error, 0);
  EXPECT_EQ(r.blockSize, 1024u);
  EXPECT_EQ(r.id, id);
  EXPECT_EQ(c.PackageState(id), ps::kTransferring);
}

TEST(UcmTransfer, StartRejectsZeroAndOversizedPackages) { // 00140
  PackageManagerCore c(PackageManagerCore::Limits{4096, 1024});
  EXPECT_EQ(c.TransferStart(0, MakeId(1)).error, E::InsufficientMemory);
  EXPECT_EQ(c.TransferStart(4097, MakeId(1)).error, E::InsufficientMemory);
  EXPECT_EQ(c.TransferStart(4096, MakeId(1)).error, 0);
}

TEST(UcmTransfer, FullUploadReachesTransferred) {
  PackageManagerCore c;
  auto id = Upload(c, 3000);
  EXPECT_EQ(c.PackageState(id), ps::kTransferred);
}

TEST(UcmTransfer, PaddedLastBlockIsAcceptedAndTrimmed) {
  PackageManagerCore c; // the OTA client pads the last block to the block size
  auto id = Upload(c, 2500, 3, /*padLast=*/true);
  EXPECT_EQ(c.PackageState(id), ps::kTransferred);
}

TEST(UcmTransfer, SizeThatIsAMultipleOfBlockSizeKeepsTheLastBlock) {
  PackageManagerCore c; // regression: size % blockSize == 0 used to drop the final block
  auto id = MakeId(4);
  auto s = c.TransferStart(2048, id);
  EXPECT_EQ(c.TransferData(id, 1024, 0).accepted, 1024u);
  EXPECT_EQ(c.TransferData(id, 1024, 1).accepted, 1024u);
  EXPECT_EQ(c.TransferExit(id), 0);
  (void)s;
}

TEST(UcmTransfer, UnknownIdIsInvalidTransferId) { // 00203 / 00212 / 00214
  PackageManagerCore c;
  auto id = MakeId(9);
  EXPECT_EQ(c.TransferData(id, 10, 0).error, E::InvalidTransferId);
  EXPECT_EQ(c.TransferExit(id), E::InvalidTransferId);
  EXPECT_EQ(c.DeleteTransfer(id), E::InvalidTransferId);
  EXPECT_EQ(c.Cancel(id), E::InvalidTransferId);
  EXPECT_EQ(c.CheckProgress(id), E::InvalidTransferId);
  EXPECT_EQ(c.BeginProcess(id), E::InvalidTransferId);
}

TEST(UcmTransfer, WrongBlockCounterIsIncorrectBlock) { // 00204
  PackageManagerCore c;
  auto id = MakeId(1);
  c.TransferStart(5000, id);
  EXPECT_EQ(c.TransferData(id, 1024, 1).error, E::IncorrectBlock); // must start at 0
  EXPECT_EQ(c.TransferData(id, 1024, 0).error, 0);
  EXPECT_EQ(c.TransferData(id, 1024, 0).error, E::IncorrectBlock); // replayed block
  EXPECT_EQ(c.TransferData(id, 1024, 1).error, 0);
}

TEST(UcmTransfer, TooBigBlockIsIncorrectBlockSize) { // 00243
  PackageManagerCore c;
  auto id = MakeId(1);
  c.TransferStart(5000, id);
  EXPECT_EQ(c.TransferData(id, 1025, 0).error, E::IncorrectBlockSize);
  EXPECT_EQ(c.TransferData(id, 1024, 0).error, 0); // a rejected block does not advance
}

TEST(UcmTransfer, MoreDataThanAnnouncedIsIncorrectSize) { // 00205
  PackageManagerCore c;
  auto id = MakeId(1);
  c.TransferStart(1024, id);
  EXPECT_EQ(c.TransferData(id, 1024, 0).error, 0);
  EXPECT_EQ(c.TransferData(id, 1, 1).error, E::IncorrectSize);
}

TEST(UcmTransfer, ExitBeforeAllDataIsInsufficientData) { // 00087
  PackageManagerCore c;
  auto id = MakeId(1);
  c.TransferStart(3000, id);
  c.TransferData(id, 1024, 0);
  EXPECT_EQ(c.TransferExit(id), E::InsufficientData);
  EXPECT_EQ(c.PackageState(id), ps::kTransferring); // still open, can continue
}

TEST(UcmTransfer, DataOrExitAfterExitIsOperationNotPermitted) { // 00208 / 00148
  PackageManagerCore c;
  auto id = Upload(c, 1000);
  EXPECT_EQ(c.TransferData(id, 10, 1).error, E::OperationNotPermitted);
  EXPECT_EQ(c.TransferExit(id), E::OperationNotPermitted);
}

TEST(UcmTransfer, ParallelTransfersAreIndependent) {
  PackageManagerCore c;
  auto a = MakeId(10), b = MakeId(50);
  c.TransferStart(2048, a);
  c.TransferStart(1024, b);
  EXPECT_EQ(c.TransferData(a, 1024, 0).error, 0);
  EXPECT_EQ(c.TransferData(b, 1024, 0).error, 0);
  EXPECT_EQ(c.TransferData(a, 1024, 1).error, 0);
  EXPECT_EQ(c.TransferExit(b), 0);
  EXPECT_EQ(c.PackageState(a), ps::kTransferring);
  EXPECT_EQ(c.PackageState(b), ps::kTransferred);
}

TEST(UcmTransfer, DeleteRemovesThePackage) {
  PackageManagerCore c;
  auto id = Upload(c, 100);
  EXPECT_EQ(c.DeleteTransfer(id), 0);
  EXPECT_EQ(c.PackageState(id), -1);
}

// -------------------------------------------------------------- processing
TEST(UcmProcessing, NotAllowedBeforeTransferExit) { // 00219
  PackageManagerCore c;
  auto id = MakeId(1);
  c.TransferStart(100, id);
  EXPECT_EQ(c.BeginProcess(id), E::OperationNotPermitted);
  EXPECT_EQ(c.Status(), st::kIdle);
}

TEST(UcmProcessing, SuccessMovesIdleToProcessingToReady) {
  PackageManagerCore c;
  auto id = Upload(c, 100);
  EXPECT_EQ(c.BeginProcess(id), 0);
  EXPECT_EQ(c.Status(), st::kProcessing);
  EXPECT_EQ(c.PackageState(id), ps::kProcessing);
  c.EndProcess(id, true);
  EXPECT_EQ(c.Status(), st::kReady);
  EXPECT_EQ(c.PackageState(id), ps::kProcessed);
}

TEST(UcmProcessing, SecondCallWhileProcessingIsServiceBusy) { // 00017
  PackageManagerCore c;
  auto a = Upload(c, 100, 1), b = Upload(c, 100, 40);
  EXPECT_EQ(c.BeginProcess(a), 0);
  EXPECT_EQ(c.BeginProcess(b), E::ServiceBusy);
  EXPECT_EQ(c.BeginProcess(a), E::ServiceBusy);
}

TEST(UcmProcessing, FailureReturnsToIdleAndAllowsRetry) {
  PackageManagerCore c;
  auto id = Upload(c, 100);
  c.BeginProcess(id);
  c.EndProcess(id, false);
  EXPECT_EQ(c.Status(), st::kIdle);
  EXPECT_EQ(c.PackageState(id), ps::kTransferred);
  EXPECT_EQ(c.BeginProcess(id), 0);
}

TEST(UcmProcessing, FailureKeepsReadyWhenAnotherPackageIsProcessed) {
  PackageManagerCore c;
  auto a = Upload(c, 100, 1), b = Upload(c, 100, 40);
  c.BeginProcess(a);
  c.EndProcess(a, true);
  c.BeginProcess(b);
  c.EndProcess(b, false);
  EXPECT_EQ(c.Status(), st::kReady);
}

TEST(UcmProcessing, DeleteWhileProcessingIsRejected) { // 00215
  PackageManagerCore c;
  auto id = Upload(c, 100);
  c.BeginProcess(id);
  EXPECT_EQ(c.DeleteTransfer(id), E::OperationNotPermitted);
}

TEST(UcmProcessing, CancelOnlyWhileProcessing) { // 00234
  PackageManagerCore c;
  auto id = Upload(c, 100);
  EXPECT_EQ(c.Cancel(id), E::OperationNotPermitted);
  c.BeginProcess(id);
  EXPECT_FALSE(c.CancelRequested(id));
  EXPECT_EQ(c.Cancel(id), 0);
  EXPECT_TRUE(c.CancelRequested(id));
}

// ------------------------------------------------- activate / rollback / etc
namespace {
PackageManagement::TransferIdType ProcessOne(PackageManagerCore &c, std::uint8_t seed = 1) {
  auto id = Upload(c, 100, seed);
  EXPECT_EQ(c.BeginProcess(id), 0);
  c.EndProcess(id, true);
  return id;
}
} // namespace

TEST(UcmActivation, ActivateOnlyInReady) { // 00241
  PackageManagerCore c;
  EXPECT_EQ(c.BeginActivate(), E::OperationNotPermitted); // kIdle
  ProcessOne(c);
  EXPECT_EQ(c.BeginActivate(), 0);
  EXPECT_EQ(c.Status(), st::kActivating);
  EXPECT_EQ(c.BeginActivate(), E::OperationNotPermitted); // already activating
  c.EndActivate(true);
  EXPECT_EQ(c.Status(), st::kActivated);
}

TEST(UcmActivation, FailedActivationReturnsToReady) { // 00258
  PackageManagerCore c;
  ProcessOne(c);
  c.BeginActivate();
  c.EndActivate(false);
  EXPECT_EQ(c.Status(), st::kReady);
}

TEST(UcmActivation, RollbackOnlyAfterActivation) { // 00239
  PackageManagerCore c;
  EXPECT_EQ(c.BeginRollback(), E::OperationNotPermitted);
  ProcessOne(c);
  EXPECT_EQ(c.BeginRollback(), E::OperationNotPermitted); // kReady
  c.BeginActivate();
  c.EndActivate(true);
  EXPECT_EQ(c.BeginRollback(), 0);
  EXPECT_EQ(c.Status(), st::kRollingBack);
  c.EndRollback(true);
  EXPECT_EQ(c.Status(), st::kRolledBack);
}

TEST(UcmActivation, RevertOnlyBeforeActivation) { // 00237
  PackageManagerCore c;
  EXPECT_EQ(c.RevertProcessed(), E::OperationNotPermitted); // kIdle
  auto id = ProcessOne(c);
  EXPECT_EQ(c.RevertProcessed(), 0);
  EXPECT_EQ(c.Status(), st::kIdle);
  EXPECT_EQ(c.PackageState(id), ps::kTransferred);
  ProcessOne(c, 40);
  c.BeginActivate();
  c.EndActivate(true);
  EXPECT_EQ(c.RevertProcessed(), E::OperationNotPermitted);
}

TEST(UcmActivation, FinishCleansUpToIdle) {
  PackageManagerCore c;
  EXPECT_EQ(c.BeginFinish(), E::OperationNotPermitted);
  auto id = ProcessOne(c);
  EXPECT_EQ(c.BeginFinish(), E::OperationNotPermitted); // kReady
  c.BeginActivate();
  c.EndActivate(true);
  EXPECT_EQ(c.BeginFinish(), 0);
  EXPECT_EQ(c.Status(), st::kCleaningUp);
  c.EndFinish();
  EXPECT_EQ(c.Status(), st::kIdle);
  EXPECT_EQ(c.PackageState(id), -1); // processed packages are forgotten
}

TEST(UcmActivation, FinishAlsoValidAfterRollback) {
  PackageManagerCore c;
  ProcessOne(c);
  c.BeginActivate();
  c.EndActivate(true);
  c.BeginRollback();
  c.EndRollback(true);
  EXPECT_EQ(c.BeginFinish(), 0);
}

TEST(UcmActivation, FullHappyPathReportsEveryStatusInOrder) {
  PackageManagerCore c;
  std::vector<int> seen;
  c.OnStatusChange([&](E::PackageManagerStatusType s) { seen.push_back(s); });
  auto id = Upload(c, 4000);
  c.BeginProcess(id);
  c.EndProcess(id, true);
  c.BeginActivate();
  c.EndActivate(true);
  c.BeginFinish();
  c.EndFinish();
  EXPECT_EQ(seen, (std::vector<int>{st::kProcessing, st::kReady, st::kActivating,
                                    st::kActivated, st::kCleaningUp, st::kIdle}));
}

// ---------------------------------------------------------------- threading
TEST(UcmThreading, ConcurrentTransfersDoNotInterfere) {
  PackageManagerCore c;
  std::atomic<int> bad{0};
  std::vector<std::thread> ts;
  for (int t = 0; t < 8; ++t)
    ts.emplace_back([&, t] {
      auto id = MakeId(static_cast<std::uint8_t>(t * 20));
      if (c.TransferStart(10 * 1024, id).error) ++bad;
      for (std::uint64_t b = 0; b < 10; ++b)
        if (c.TransferData(id, 1024, b).error) ++bad;
      if (c.TransferExit(id)) ++bad;
    });
  for (auto &t : ts) t.join();
  EXPECT_EQ(bad.load(), 0);
}

TEST(UcmThreading, OnlyOneCallerWinsProcessing) {
  PackageManagerCore c;
  std::vector<PackageManagement::TransferIdType> ids;
  for (int i = 0; i < 8; ++i) ids.push_back(Upload(c, 10, static_cast<std::uint8_t>(i * 20)));
  std::atomic<int> won{0};
  std::vector<std::thread> ts;
  for (auto &id : ids) ts.emplace_back([&] { if (c.BeginProcess(id) == 0) ++won; });
  for (auto &t : ts) t.join();
  EXPECT_EQ(won.load(), 1);
}

// ------------------------------------------------------------------ parsing
TEST(UcmParsing, ParseByteHandlesHexDecimalAndGarbage) {
  EXPECT_EQ(core::ParseByte("0x01"), 1);
  EXPECT_EQ(core::ParseByte("0x03"), 3);
  EXPECT_EQ(core::ParseByte("2"), 2);
  EXPECT_EQ(core::ParseByte("0xFF"), 255);
  EXPECT_EQ(core::ParseByte("0x1FF"), 255); // clamped
  EXPECT_EQ(core::ParseByte(""), 0);
  EXPECT_EQ(core::ParseByte("zz"), 0);
}

TEST(UcmParsing, ParseU64) {
  EXPECT_EQ(core::ParseU64("1024"), 1024u);
  EXPECT_EQ(core::ParseU64("18446744073709551615"), 18446744073709551615ULL);
  EXPECT_EQ(core::ParseU64(""), 0u);
  EXPECT_EQ(core::ParseU64("abc"), 0u);
}
