#include <thread>

#include "per_fixture.h"

using ara::per::PerErrc;
namespace fs = std::filesystem;
using pertest::PerFixture;
using KvsTest = PerFixture;

namespace {
auto Open(char const *spec = "App/Kvs") {
  return ara::per::OpenKeyValueStorage(ara::core::InstanceSpecifier(spec));
}
} // namespace

TEST_F(KvsTest, StoresAndReadsSupportedTypes) {
  auto kvs = Open().Value();
  ASSERT_TRUE(kvs->SetValue("i", 42).HasValue());
  ASSERT_TRUE(kvs->SetValue("d", 2.5).HasValue());
  ASSERT_TRUE(kvs->SetValue("s", std::string("hello")).HasValue());
  ASSERT_TRUE(kvs->SetValue("b", true).HasValue());
  ASSERT_TRUE(kvs->SetValue("v", std::vector<int>{1, 2, 3}).HasValue());
  ASSERT_TRUE(kvs->SetValue("m", std::map<std::string, int>{{"x", 1}}).HasValue());
  EXPECT_EQ(kvs->GetValue<int>("i").Value(), 42);
  EXPECT_DOUBLE_EQ(kvs->GetValue<double>("d").Value(), 2.5);
  EXPECT_EQ(kvs->GetValue<std::string>("s").Value(), "hello");
  EXPECT_TRUE(kvs->GetValue<bool>("b").Value());
  EXPECT_EQ(kvs->GetValue<std::vector<int>>("v").Value(), (std::vector<int>{1, 2, 3}));
  auto const m = kvs->GetValue<std::map<std::string, int>>("m").Value();
  EXPECT_EQ(m.at("x"), 1);
}

TEST_F(KvsTest, TypeMismatchIsDetected) {
  auto kvs = Open().Value();
  ASSERT_TRUE(kvs->SetValue("s", std::string("text")).HasValue());
  ASSERT_TRUE(kvs->SetValue("big", 300).HasValue());
  ASSERT_TRUE(kvs->SetValue("neg", -1).HasValue());
  ASSERT_TRUE(kvs->SetValue("flag", true).HasValue());
  ASSERT_TRUE(kvs->SetValue("num", 1).HasValue());
  EXPECT_TRUE(kvs->GetValue<int>("s").CheckError(PerErrc::kDataTypeMismatch));
  EXPECT_TRUE(kvs->GetValue<std::uint8_t>("big").CheckError(PerErrc::kDataTypeMismatch));
  EXPECT_TRUE(kvs->GetValue<std::uint32_t>("neg").CheckError(PerErrc::kDataTypeMismatch));
  EXPECT_TRUE(kvs->GetValue<int>("flag").CheckError(PerErrc::kDataTypeMismatch));
  EXPECT_TRUE(kvs->GetValue<bool>("num").CheckError(PerErrc::kDataTypeMismatch));
  EXPECT_TRUE(kvs->GetValue<std::string>("num").CheckError(PerErrc::kDataTypeMismatch));
  EXPECT_EQ(kvs->GetValue<std::int64_t>("big").Value(), 300);
}

TEST_F(KvsTest, KeyManagement) {
  auto kvs = Open().Value();
  EXPECT_TRUE(kvs->GetValue<int>("missing").CheckError(PerErrc::kKeyNotFound));
  EXPECT_FALSE(kvs->KeyExists("missing").Value());
  EXPECT_TRUE(kvs->KeyExists("volume").Value()); // initial value
  ASSERT_TRUE(kvs->SetValue("a", 1).HasValue());
  auto keys = kvs->GetAllKeys().Value();
  EXPECT_EQ(keys.size(), 3U); // name, volume, a
  EXPECT_TRUE(kvs->RemoveKey("a").HasValue());
  EXPECT_TRUE(kvs->RemoveKey("a").CheckError(PerErrc::kKeyNotFound));
  EXPECT_TRUE(kvs->RemoveAllKeys().HasValue());
  EXPECT_TRUE(kvs->GetAllKeys().Value().empty());
  EXPECT_TRUE(kvs->SetValue("", 1).CheckError(PerErrc::kUnsupported));
}

TEST_F(KvsTest, SyncedChangesSurviveReopenUnsyncedDoNot) {
  {
    auto kvs = Open().Value();
    ASSERT_TRUE(kvs->SetValue("kept", 1).HasValue());
    ASSERT_TRUE(kvs->SyncToStorage().HasValue());
    ASSERT_TRUE(kvs->SetValue("lost", 2).HasValue());
    EXPECT_TRUE(kvs->HasPendingChanges());
  } // handle released, no implicit sync
  auto again = Open().Value();
  EXPECT_EQ(again->GetValue<int>("kept").Value(), 1);
  EXPECT_TRUE(again->GetValue<int>("lost").CheckError(PerErrc::kKeyNotFound));
  EXPECT_FALSE(again->HasPendingChanges());
}

TEST_F(KvsTest, SameSpecifierYieldsSameStorage) {
  auto a = Open().Value();
  auto b = Open().Value();
  EXPECT_EQ(a.get(), b.get());
  ASSERT_TRUE(a->SetValue("shared", 9).HasValue());
  EXPECT_EQ(b->GetValue<int>("shared").Value(), 9);
}

TEST_F(KvsTest, DiscardPendingChangesRollsBack) {
  auto kvs = Open().Value();
  ASSERT_TRUE(kvs->SetValue("x", 1).HasValue());
  ASSERT_TRUE(kvs->SyncToStorage().HasValue());
  ASSERT_TRUE(kvs->SetValue("x", 2).HasValue());
  ASSERT_TRUE(kvs->RemoveKey("volume").HasValue());
  ASSERT_TRUE(kvs->DiscardPendingChanges().HasValue());
  EXPECT_EQ(kvs->GetValue<int>("x").Value(), 1);
  EXPECT_TRUE(kvs->KeyExists("volume").Value());
}

TEST_F(KvsTest, InitialValuesAndResetKey) {
  auto kvs = Open().Value();
  EXPECT_EQ(kvs->GetValue<int>("volume").Value(), 5);
  EXPECT_EQ(kvs->GetValue<std::string>("name").Value(), "default");
  ASSERT_TRUE(kvs->SetValue("volume", 99).HasValue());
  ASSERT_TRUE(kvs->ResetKey("volume").HasValue());
  EXPECT_EQ(kvs->GetValue<int>("volume").Value(), 5);
  EXPECT_TRUE(kvs->ResetKey("nothing").CheckError(PerErrc::kInitValueNotAvailable));
}

TEST_F(KvsTest, QuotaIsEnforcedAndStateUnchanged) {
  Install(R"([{"specifier":"App/Small","type":"kvs","maxSize":40}])");
  auto kvs = Open("App/Small").Value();
  ASSERT_TRUE(kvs->SetValue("a", 1).HasValue());
  auto big = kvs->SetValue("b", std::string(100, 'x'));
  EXPECT_TRUE(big.CheckError(PerErrc::kQuotaExceeded));
  EXPECT_FALSE(kvs->KeyExists("b").Value());
  EXPECT_LE(kvs->GetCurrentStorageSize().Value(), 40U);
}

TEST_F(KvsTest, ReadOnlyStorageRejectsWrites) {
  Install(R"([{"specifier":"App/Ro","type":"kvs","readOnly":true,"initialValues":{"k":1}}])");
  auto kvs = Open("App/Ro").Value();
  EXPECT_EQ(kvs->GetValue<int>("k").Value(), 1);
  EXPECT_TRUE(kvs->SetValue("k", 2).CheckError(PerErrc::kIllegalWriteAccess));
  EXPECT_TRUE(kvs->RemoveKey("k").CheckError(PerErrc::kIllegalWriteAccess));
  EXPECT_TRUE(kvs->RemoveAllKeys().CheckError(PerErrc::kIllegalWriteAccess));
  EXPECT_TRUE(kvs->SyncToStorage().CheckError(PerErrc::kIllegalWriteAccess));
}

TEST_F(KvsTest, UnknownSpecifier) {
  EXPECT_TRUE(Open("App/Nope").CheckError(PerErrc::kStorageNotFound));
  // a file storage is not a key-value storage
  EXPECT_TRUE(Open("App/Files").CheckError(PerErrc::kStorageNotFound));
}

// ---- integrity / redundancy -------------------------------------------------
namespace {
void Overwrite(std::filesystem::path const &p, std::string const &content) {
  std::ofstream(p, std::ios::trunc) << content;
}
} // namespace

TEST_F(KvsTest, CorruptedPrimaryFallsBackToBackup) {
  {
    auto kvs = Open().Value();
    ASSERT_TRUE(kvs->SetValue("gen", 1).HasValue());
    ASSERT_TRUE(kvs->SyncToStorage().HasValue());
    ASSERT_TRUE(kvs->SetValue("gen", 2).HasValue());
    ASSERT_TRUE(kvs->SyncToStorage().HasValue()); // now .bak holds gen==1
  }
  ASSERT_TRUE(fs::exists(KvsFile().string() + ".bak"));
  Overwrite(KvsFile(), "garbage {");
  auto kvs = Open().Value();
  EXPECT_EQ(kvs->GetValue<int>("gen").Value(), 1);
}

TEST_F(KvsTest, CrcDetectsSilentTampering) {
  {
    auto kvs = Open().Value();
    ASSERT_TRUE(kvs->SetValue("secret", 1).HasValue());
    ASSERT_TRUE(kvs->SyncToStorage().HasValue());
  }
  std::ifstream in(KvsFile());
  std::stringstream ss;
  ss << in.rdbuf();
  in.close();
  std::string text = ss.str();
  auto pos = text.find("\"secret\": 1");
  ASSERT_NE(pos, std::string::npos);
  text.replace(pos, 11, "\"secret\": 2"); // valid JSON, wrong CRC
  Overwrite(KvsFile(), text);
  EXPECT_TRUE(Open().CheckError(PerErrc::kIntegrityCorrupted)); // no backup yet
}

TEST_F(KvsTest, RecoverAndResetAfterCorruption) {
  {
    auto kvs = Open().Value();
    ASSERT_TRUE(kvs->SetValue("v", 1).HasValue());
    ASSERT_TRUE(kvs->SyncToStorage().HasValue());
    ASSERT_TRUE(kvs->SetValue("v", 2).HasValue());
    ASSERT_TRUE(kvs->SyncToStorage().HasValue());
  }
  Overwrite(KvsFile(), "x");
  Overwrite(KvsFile().string() + ".bak", "y");
  EXPECT_TRUE(Open().CheckError(PerErrc::kIntegrityCorrupted));
  EXPECT_TRUE(ara::per::RecoverKeyValueStorage(Spec("App/Kvs")).CheckError(PerErrc::kValidationFailed));
  ASSERT_TRUE(ara::per::ResetKeyValueStorage(Spec("App/Kvs")).HasValue());
  auto kvs = Open().Value();
  EXPECT_EQ(kvs->GetValue<int>("volume").Value(), 5); // back to initial values
  EXPECT_FALSE(kvs->KeyExists("v").Value());
}

TEST_F(KvsTest, RecoverRestoresFromValidBackup) {
  {
    auto kvs = Open().Value();
    ASSERT_TRUE(kvs->SetValue("v", 1).HasValue());
    ASSERT_TRUE(kvs->SyncToStorage().HasValue());
    ASSERT_TRUE(kvs->SetValue("v", 2).HasValue());
    ASSERT_TRUE(kvs->SyncToStorage().HasValue());
  }
  Overwrite(KvsFile(), "broken");
  ASSERT_TRUE(ara::per::RecoverKeyValueStorage(Spec("App/Kvs")).HasValue());
  EXPECT_EQ(Open().Value()->GetValue<int>("v").Value(), 1);
}

TEST_F(KvsTest, ResetAndRecoverRequireClosedStorage) {
  auto kvs = Open().Value();
  EXPECT_TRUE(ara::per::ResetKeyValueStorage(Spec("App/Kvs")).CheckError(PerErrc::kResourceBusy));
  EXPECT_TRUE(ara::per::RecoverKeyValueStorage(Spec("App/Kvs")).CheckError(PerErrc::kResourceBusy));
  EXPECT_TRUE(ara::per::Configure(*ara::per::PersistencyConfig::FromJson(EmptyConfig()).Ok())
                  .CheckError(PerErrc::kResourceBusy));
}

TEST_F(KvsTest, ConcurrentWritersAreSafe) {
  auto kvs = Open().Value();
  constexpr int kThreads = 8, kPerThread = 100;
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < kPerThread; ++i) {
        ASSERT_TRUE(kvs->SetValue("t" + std::to_string(t) + "_" + std::to_string(i), i).HasValue());
      }
    });
  }
  for (auto &th : threads) th.join();
  EXPECT_EQ(kvs->GetAllKeys().Value().size(), static_cast<std::size_t>(kThreads * kPerThread + 2));
  ASSERT_TRUE(kvs->SyncToStorage().HasValue());
}

TEST_F(KvsTest, ResetPersistencyRestoresEverything) {
  {
    auto kvs = Open().Value();
    ASSERT_TRUE(kvs->SetValue("volume", 77).HasValue());
    ASSERT_TRUE(kvs->SyncToStorage().HasValue());
  }
  ASSERT_TRUE(ara::per::ResetPersistency().HasValue());
  EXPECT_EQ(Open().Value()->GetValue<int>("volume").Value(), 5);
}

TEST(PersistencyConfig, RejectsInvalidDocuments) {
  using ara::per::PersistencyConfig;
  EXPECT_FALSE(PersistencyConfig::FromJson("not json").HasValue());
  EXPECT_FALSE(PersistencyConfig::FromJson("{}").HasValue());
  EXPECT_FALSE(PersistencyConfig::FromJson(R"({"storageRoot":"/tmp","storages":{}})").HasValue());
  EXPECT_FALSE(PersistencyConfig::FromJson(R"({"storageRoot":"/tmp","storages":[{"specifier":"bad id","type":"kvs"}]})").HasValue());
  EXPECT_FALSE(PersistencyConfig::FromJson(R"({"storageRoot":"/tmp","storages":[{"specifier":"A/b","type":"db"}]})").HasValue());
  EXPECT_FALSE(PersistencyConfig::FromFile("/definitely/not/here.json").HasValue());
  EXPECT_TRUE(PersistencyConfig::FromJson(R"({"storageRoot":"/tmp"})").HasValue());
}

TEST(PerErrorDomain, MessagesAndThrow) {
  auto ec = ara::per::MakeErrorCode(PerErrc::kKeyNotFound);
  EXPECT_STREQ(ec.Domain().Name(), "Per");
  EXPECT_STREQ(ec.Message(), "key not found");
  EXPECT_THROW(ec.ThrowAsException(), ara::per::PerException);
}
