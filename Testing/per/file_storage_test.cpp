#include "per_fixture.h"

using ara::per::OpenMode;
using ara::per::PerErrc;
namespace fs = std::filesystem;
using pertest::PerFixture;
using FileTest = PerFixture;

namespace {
auto Open(char const *spec = "App/Files") {
  return ara::per::OpenFileStorage(ara::core::InstanceSpecifier(spec));
}
} // namespace

TEST_F(FileTest, WriteThenReadText) {
  auto st = Open().Value();
  {
    auto w = st->OpenFileReadWrite("a.txt").Value();
    ASSERT_TRUE(w->WriteText("hello\nworld\n").HasValue());
    ASSERT_TRUE(w->SyncToFile().HasValue());
    EXPECT_EQ(w->GetSize(), 12U);
    EXPECT_EQ(w->GetPosition(), 12U);
  }
  auto r = st->OpenFileReadOnly("a.txt").Value();
  EXPECT_EQ(r->ReadLine().Value(), "hello");
  EXPECT_EQ(r->ReadLine().Value(), "world");
  EXPECT_TRUE(r->IsEof());
  EXPECT_TRUE(r->ReadLine().CheckError(PerErrc::kEof));
  EXPECT_TRUE(r->GetChar().CheckError(PerErrc::kEof));
}

TEST_F(FileTest, BinaryAndCharAccess) {
  auto st = Open().Value();
  std::vector<ara::core::Byte> bytes{ara::core::Byte{0}, ara::core::Byte{255}, ara::core::Byte{7}};
  {
    auto w = st->OpenFileWriteOnly("b.bin").Value();
    ASSERT_TRUE(w->WriteBinary(bytes).HasValue());
    EXPECT_TRUE(w->ReadText().CheckError(PerErrc::kUnsupported)); // write-only
  }
  auto r = st->OpenFileReadOnly("b.bin").Value();
  EXPECT_EQ(r->PeekByte().Value(), ara::core::Byte{0});
  EXPECT_EQ(r->GetPosition(), 0U); // peek does not advance
  EXPECT_EQ(r->GetByte().Value(), ara::core::Byte{0});
  EXPECT_EQ(r->ReadBinary().Value(), (std::vector<ara::core::Byte>{ara::core::Byte{255}, ara::core::Byte{7}}));
}

TEST_F(FileTest, PositioningAndPartialReads) {
  auto st = Open().Value();
  {
    auto w = st->OpenFileWriteOnly("p.txt").Value();
    ASSERT_TRUE(w->WriteText("0123456789").HasValue());
  }
  auto r = st->OpenFileReadOnly("p.txt").Value();
  EXPECT_EQ(r->ReadText(4).Value(), "0123");
  ASSERT_TRUE(r->SetPosition(8).HasValue());
  EXPECT_EQ(r->ReadText(100).Value(), "89");
  ASSERT_TRUE(r->MovePosition(ara::per::ReadAccessor::Origin::kEnd, -3).HasValue());
  EXPECT_EQ(r->GetChar().Value(), '7');
  ASSERT_TRUE(r->MovePosition(ara::per::ReadAccessor::Origin::kCurrent, -2).HasValue());
  EXPECT_EQ(r->GetChar().Value(), '6');
  EXPECT_TRUE(r->SetPosition(11).CheckError(PerErrc::kInvalidPosition));
  EXPECT_TRUE(r->MovePosition(ara::per::ReadAccessor::Origin::kBeginning, -1).CheckError(PerErrc::kInvalidPosition));
}

TEST_F(FileTest, OpenModes) {
  auto st = Open().Value();
  {
    auto w = st->OpenFileWriteOnly("m.txt").Value();
    ASSERT_TRUE(w->WriteText("abc").HasValue());
  }
  { // append
    auto w = st->OpenFileReadWrite("m.txt", OpenMode::kAppend).Value();
    ASSERT_TRUE(w->SetPosition(0).HasValue());
    ASSERT_TRUE(w->WriteText("def").HasValue()); // still lands at the end
  }
  EXPECT_EQ(st->OpenFileReadOnly("m.txt").Value()->ReadText().Value(), "abcdef");
  { // at the end
    auto w = st->OpenFileReadWrite("m.txt", OpenMode::kAtTheEnd).Value();
    ASSERT_TRUE((*w << "g").Good());
  }
  EXPECT_EQ(st->OpenFileReadOnly("m.txt").Value()->ReadText().Value(), "abcdefg");
  { // overwrite from the beginning
    auto w = st->OpenFileReadWrite("m.txt").Value();
    ASSERT_TRUE(w->WriteText("XY").HasValue());
  }
  EXPECT_EQ(st->OpenFileReadOnly("m.txt").Value()->ReadText().Value(), "XYcdefg");
  { // truncate
    auto w = st->OpenFileReadWrite("m.txt", OpenMode::kTruncate).Value();
    EXPECT_EQ(w->GetSize(), 0U);
  }
  EXPECT_TRUE(st->OpenFileReadOnly("m.txt", OpenMode::kTruncate).CheckError(PerErrc::kInvalidOpenMode));
  EXPECT_TRUE(st->OpenFileReadOnly("m.txt", OpenMode::kAppend).CheckError(PerErrc::kInvalidOpenMode));
  EXPECT_TRUE(st->OpenFileReadWrite("m.txt", OpenMode::kAtTheBeginning | OpenMode::kAtTheEnd)
                  .CheckError(PerErrc::kInvalidOpenMode));
}

TEST_F(FileTest, FileCanBeOpenedOnlyOnce) {
  auto st = Open().Value();
  auto w = st->OpenFileReadWrite("busy").Value();
  EXPECT_TRUE(st->OpenFileReadOnly("busy").CheckError(PerErrc::kResourceBusy));
  EXPECT_TRUE(st->OpenFileReadWrite("busy").CheckError(PerErrc::kResourceBusy));
  EXPECT_TRUE(st->DeleteFile("busy").CheckError(PerErrc::kResourceBusy));
  w.reset();
  EXPECT_TRUE(st->DeleteFile("busy").HasValue());
  EXPECT_FALSE(st->FileExists("busy").Value());
  EXPECT_TRUE(st->DeleteFile("busy").CheckError(PerErrc::kFileNotFound));
}

TEST_F(FileTest, ListingAndInfo) {
  auto st = Open().Value();
  for (char const *n : {"c", "a", "b"}) {
    auto w = st->OpenFileWriteOnly(n).Value();
    ASSERT_TRUE(w->WriteText("12345").HasValue());
  }
  EXPECT_EQ(st->GetAllFileNames().Value(), (std::vector<std::string>{"a", "b", "c"}));
  EXPECT_EQ(st->GetCurrentFileSize("a").Value(), 5U);
  EXPECT_EQ(st->GetFileInfo("a").Value().fileSize, 5U);
  EXPECT_EQ(st->GetCurrentStorageSize().Value(), 15U);
  EXPECT_TRUE(st->GetCurrentFileSize("zzz").CheckError(PerErrc::kFileNotFound));
}

TEST_F(FileTest, QuotasAreEnforced) {
  auto st = Open().Value(); // maxFiles = 3, maxSize = 64
  for (char const *n : {"f1", "f2", "f3"}) {
    ASSERT_TRUE(st->OpenFileWriteOnly(n).HasValue());
  }
  EXPECT_TRUE(st->OpenFileWriteOnly("f4").CheckError(PerErrc::kTooManyFiles));
  auto w = st->OpenFileReadWrite("f1").Value();
  ASSERT_TRUE(w->WriteText(std::string(60, 'x')).HasValue());
  EXPECT_TRUE(w->WriteText(std::string(10, 'y')).CheckError(PerErrc::kQuotaExceeded));
  EXPECT_EQ(w->GetSize(), 60U); // failed write left the file untouched
  EXPECT_TRUE(w->SetFileSize(100).CheckError(PerErrc::kQuotaExceeded));
  ASSERT_TRUE(w->SetFileSize(10).HasValue());
  EXPECT_EQ(w->GetSize(), 10U);
  EXPECT_EQ(w->GetPosition(), 10U);
  EXPECT_FALSE((*w << std::string(100, 'z')).Good());
}

TEST_F(FileTest, InvalidFileNames) {
  auto st = Open().Value();
  for (char const *n : {"", "..", ".", "a/b", "../escape"}) {
    EXPECT_TRUE(st->OpenFileReadWrite(n).CheckError(PerErrc::kFileNotFound)) << n;
    EXPECT_TRUE(st->DeleteFile(n).CheckError(PerErrc::kFileNotFound)) << n;
    EXPECT_FALSE(st->FileExists(n).Value()) << n;
  }
  EXPECT_FALSE(fs::exists(root / "escape"));
}

TEST_F(FileTest, ReadOnlyOpenOfMissingFile) {
  auto st = Open().Value();
  EXPECT_TRUE(st->OpenFileReadOnly("nope").CheckError(PerErrc::kFileNotFound));
}

TEST_F(FileTest, ReadOnlyStorage) {
  Install(R"([{"specifier":"App/RoFiles","type":"file","readOnly":true}])");
  auto st = Open("App/RoFiles").Value();
  EXPECT_TRUE(st->OpenFileReadWrite("x").CheckError(PerErrc::kIllegalWriteAccess));
  EXPECT_TRUE(st->OpenFileWriteOnly("x").CheckError(PerErrc::kIllegalWriteAccess));
  EXPECT_TRUE(st->DeleteFile("x").CheckError(PerErrc::kIllegalWriteAccess));
}

TEST_F(FileTest, ResetFileFromInitialContent) {
  auto st = Open().Value();
  EXPECT_TRUE(st->ResetFile("cfg").CheckError(PerErrc::kInitValueNotAvailable));
  fs::create_directories(root / "App.Files" / "initial");
  std::ofstream(root / "App.Files" / "initial" / "cfg") << "factory";
  {
    auto w = st->OpenFileWriteOnly("cfg").Value();
    ASSERT_TRUE(w->WriteText("modified").HasValue());
  }
  ASSERT_TRUE(st->ResetFile("cfg").HasValue());
  EXPECT_EQ(st->OpenFileReadOnly("cfg").Value()->ReadText().Value(), "factory");
  EXPECT_TRUE(st->RecoverFile("cfg").CheckError(PerErrc::kValidationFailed));
}

TEST_F(FileTest, ResetAllFilesRestoresInitialSet) {
  fs::create_directories(root / "App.Files" / "initial");
  std::ofstream(root / "App.Files" / "initial" / "seed") << "s";
  {
    auto st = Open().Value();
    auto w = st->OpenFileWriteOnly("extra").Value();
    ASSERT_TRUE(w->WriteText("x").HasValue());
  }
  ASSERT_TRUE(ara::per::ResetAllFiles(Spec("App/Files")).HasValue());
  EXPECT_EQ(Open().Value()->GetAllFileNames().Value(), (std::vector<std::string>{"seed"}));
}

TEST_F(FileTest, DataPersistsAcrossReopen) {
  {
    auto st = Open().Value();
    auto w = st->OpenFileWriteOnly("keep").Value();
    ASSERT_TRUE(w->WriteText("durable").HasValue());
    ASSERT_TRUE(w->SyncToFile().HasValue());
  }
  EXPECT_EQ(Open().Value()->OpenFileReadOnly("keep").Value()->ReadText().Value(), "durable");
}
