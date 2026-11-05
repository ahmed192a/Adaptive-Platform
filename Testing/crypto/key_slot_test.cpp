#include <gtest/gtest.h>

#include <filesystem>
#include <random>
#include <atomic>
#include <thread>

#include "ara/crypto/key_slot.h"
#include "ara/per/persistency.h"

using namespace ara::crypto;
namespace fs = std::filesystem;

namespace {

ByteVector Bytes(std::string const &s) { return ByteVector(s.begin(), s.end()); }

KeySlotPrototype Proto(std::string name, SlotAlgorithm alg, std::uint32_t usage, bool exportable = false,
                       bool persistent = false) {
  return KeySlotPrototype{std::move(name), alg, usage, exportable, persistent};
}

class KeySlotTest : public ::testing::Test {
protected:
  void SetUp() override {
    std::random_device rd;
    root = fs::temp_directory_path() / ("ara_keyslot_ut_" + std::to_string(rd()));
    fs::create_directories(root);
    wrapKey = GenerateRandom(32).Value();
    Boot();
  }
  void TearDown() override {
    provider.reset();
    kvs.reset();
    (void)ara::per::Configure(
        ara::per::PersistencyConfig::FromJson(R"({"storageRoot":")" + (root / "unused").string() + R"("})").Value());
    std::error_code ec;
    fs::remove_all(root, ec);
  }

  /// Simulates a reboot: drops the provider and storage handle and opens them again.
  void Boot() {
    provider.reset();
    kvs.reset();
    auto cfg = ara::per::PersistencyConfig::FromJson(
        R"({"storageRoot":")" + root.string() + R"(","storages":[{"specifier":"Sec/Keys","type":"kvs"}]})");
    ASSERT_TRUE(cfg.HasValue());
    ASSERT_TRUE(ara::per::Configure(std::move(cfg).Value()).HasValue());
    kvs = ara::per::OpenKeyValueStorage(ara::core::InstanceSpecifier("Sec/Keys")).Value();
    auto p = KeyStorageProvider::Create(kvs, wrapKey);
    ASSERT_TRUE(p.HasValue());
    provider = std::make_unique<KeyStorageProvider>(std::move(p).Value());
  }

  fs::path root;
  ByteVector wrapKey;
  ara::per::SharedHandle<ara::per::KeyValueStorage> kvs;
  std::unique_ptr<KeyStorageProvider> provider;
};

} // namespace

// ---------------------------------------------------------- prototypes
TEST_F(KeySlotTest, DefineValidatesPrototype) {
  EXPECT_TRUE(provider->DefineSlot(Proto("", SlotAlgorithm::kAes256Gcm, kSlotEncrypt)).CheckError(CryptoErrc::kInvalidArgument));
  EXPECT_TRUE(provider->DefineSlot(Proto("bad name", SlotAlgorithm::kAes256Gcm, kSlotEncrypt)).CheckError(CryptoErrc::kInvalidArgument));
  EXPECT_TRUE(provider->DefineSlot(Proto("a", SlotAlgorithm::kAes256Gcm, 0)).CheckError(CryptoErrc::kIncompatibleArguments));
  EXPECT_TRUE(provider->DefineSlot(Proto("a", SlotAlgorithm::kAes256Gcm, kSlotSign)).CheckError(CryptoErrc::kIncompatibleArguments));
  EXPECT_TRUE(provider->DefineSlot(Proto("a", SlotAlgorithm::kEd25519, kSlotMac)).CheckError(CryptoErrc::kIncompatibleArguments));
  EXPECT_TRUE(provider->DefineSlot(Proto("a", SlotAlgorithm::kEd25519, kSlotSign, true)).CheckError(CryptoErrc::kUnsupported));
  ASSERT_TRUE(provider->DefineSlot(Proto("ok", SlotAlgorithm::kAes256Gcm, kSlotEncrypt | kSlotDecrypt)).HasValue());
  EXPECT_TRUE(provider->DefineSlot(Proto("ok", SlotAlgorithm::kAes256Gcm, kSlotEncrypt)).CheckError(CryptoErrc::kInvalidArgument));
  auto info = provider->GetSlotInfo("ok").Value();
  EXPECT_EQ(info.state, SlotState::kEmpty);
  EXPECT_EQ(provider->ListSlots().size(), 1U);
  EXPECT_TRUE(provider->GetSlotInfo("nope").CheckError(CryptoErrc::kUnknownIdentifier));
}

TEST(KeySlotVolatileTest, RejectsPersistentSlots) {
  auto p = KeyStorageProvider::CreateVolatile();
  EXPECT_TRUE(p.DefineSlot(Proto("k", SlotAlgorithm::kAes256Gcm, kSlotEncrypt, false, true)).CheckError(CryptoErrc::kUnsupported));
  ASSERT_TRUE(p.DefineSlot(Proto("k", SlotAlgorithm::kAes256Gcm, kSlotEncrypt | kSlotDecrypt)).HasValue());
  ASSERT_TRUE(p.Generate("k").HasValue());
  auto sealed = p.Seal("k", Bytes("aad"), Bytes("hi"));
  ASSERT_TRUE(sealed.HasValue());
  EXPECT_EQ(p.Open("k", Bytes("aad"), sealed.Value()).Value(), Bytes("hi"));
}

TEST(KeySlotCreateTest, ValidatesArguments) {
  EXPECT_TRUE(KeyStorageProvider::Create(nullptr, ByteVector(32, 1)).CheckError(CryptoErrc::kInvalidArgument));
}

// ------------------------------------------------------ AES / usage rules
TEST_F(KeySlotTest, AeadRoundTripAndEmptySlot) {
  ASSERT_TRUE(provider->DefineSlot(Proto("aes", SlotAlgorithm::kAes256Gcm, kSlotEncrypt | kSlotDecrypt)).HasValue());
  EXPECT_TRUE(provider->Seal("aes", Bytes(""), Bytes("x")).CheckError(CryptoErrc::kEmptyContainer));
  ASSERT_TRUE(provider->Generate("aes").HasValue());
  EXPECT_EQ(provider->GetSlotInfo("aes").Value().state, SlotState::kOccupied);
  auto sealed = provider->Seal("aes", Bytes("hdr"), Bytes("secret")).Value();
  EXPECT_EQ(provider->Open("aes", Bytes("hdr"), sealed).Value(), Bytes("secret"));
  EXPECT_TRUE(provider->Open("aes", Bytes("other"), sealed).CheckError(CryptoErrc::kAuthTagNotValid));
  EXPECT_TRUE(provider->Seal("missing", Bytes(""), Bytes("x")).CheckError(CryptoErrc::kUnknownIdentifier));
}

TEST_F(KeySlotTest, EncryptOnlyKeyCannotDecrypt) {
  ASSERT_TRUE(provider->DefineSlot(Proto("enc", SlotAlgorithm::kAes256Gcm, kSlotEncrypt)).HasValue());
  ASSERT_TRUE(provider->Generate("enc").HasValue());
  auto sealed = provider->Seal("enc", Bytes(""), Bytes("x")).Value();
  EXPECT_TRUE(provider->Open("enc", Bytes(""), sealed).CheckError(CryptoErrc::kUsageViolation));
  EXPECT_TRUE(provider->ComputeMac("enc", Bytes("x")).CheckError(CryptoErrc::kUsageViolation));
}

TEST_F(KeySlotTest, ImportChecksKeyLength) {
  ASSERT_TRUE(provider->DefineSlot(Proto("aes", SlotAlgorithm::kAes256Gcm, kSlotEncrypt | kSlotDecrypt)).HasValue());
  EXPECT_TRUE(provider->Import("aes", ByteVector(16, 1)).CheckError(CryptoErrc::kInvalidKeyLength));
  EXPECT_EQ(provider->GetSlotInfo("aes").Value().state, SlotState::kEmpty);
  ASSERT_TRUE(provider->Import("aes", ByteVector(32, 7)).HasValue());
  ASSERT_TRUE(provider->DefineSlot(Proto("mac", SlotAlgorithm::kHmacSha256, kSlotMac)).HasValue());
  EXPECT_TRUE(provider->Import("mac", ByteVector(8, 1)).CheckError(CryptoErrc::kInvalidKeyLength));
}

// ------------------------------------------------------------------- HMAC
TEST_F(KeySlotTest, MacComputeAndVerify) {
  ASSERT_TRUE(provider->DefineSlot(Proto("mac", SlotAlgorithm::kHmacSha256, kSlotMac)).HasValue());
  ASSERT_TRUE(provider->Import("mac", Bytes("0123456789abcdef0123456789abcdef")).HasValue());
  auto tag = provider->ComputeMac("mac", Bytes("msg")).Value();
  EXPECT_EQ(tag.size(), 32U);
  EXPECT_TRUE(provider->VerifyMac("mac", Bytes("msg"), tag).Value());
  EXPECT_FALSE(provider->VerifyMac("mac", Bytes("msG"), tag).Value());
  EXPECT_TRUE(provider->Seal("mac", Bytes(""), Bytes("x")).CheckError(CryptoErrc::kUsageViolation));
}

// ------------------------------------------------------------- signatures
TEST_F(KeySlotTest, SignOnlyCannotVerifyAndPublicKeyVerifies) {
  for (auto alg : {SlotAlgorithm::kEd25519, SlotAlgorithm::kEcdsaP256}) {
    std::string const n = alg == SlotAlgorithm::kEd25519 ? "ed" : "ec";
    ASSERT_TRUE(provider->DefineSlot(Proto(n, alg, kSlotSign)).HasValue());
    ASSERT_TRUE(provider->Generate(n).HasValue());
    auto sig = provider->Sign(n, Bytes("firmware")).Value();
    EXPECT_TRUE(provider->Verify(n, Bytes("firmware"), sig).CheckError(CryptoErrc::kUsageViolation));
    auto pub = PublicKey::FromDer(provider->GetPublicKeyDer(n).Value());
    ASSERT_TRUE(pub.HasValue());
    EXPECT_TRUE(pub.Value().Verify(Bytes("firmware"), sig).Value());
    EXPECT_FALSE(pub.Value().Verify(Bytes("firmwarX"), sig).Value());
    EXPECT_TRUE(provider->Seal(n, Bytes(""), Bytes("x")).CheckError(CryptoErrc::kUsageViolation));
  }
}

TEST_F(KeySlotTest, SignAndVerifySlot) {
  ASSERT_TRUE(provider->DefineSlot(Proto("ed", SlotAlgorithm::kEd25519, kSlotSign | kSlotVerify)).HasValue());
  ASSERT_TRUE(provider->Generate("ed").HasValue());
  auto sig = provider->Sign("ed", Bytes("m")).Value();
  EXPECT_TRUE(provider->Verify("ed", Bytes("m"), sig).Value());
  EXPECT_FALSE(provider->Verify("ed", Bytes("n"), sig).Value());
  EXPECT_TRUE(provider->GetPublicKeyDer("ed").HasValue());
  EXPECT_TRUE(provider->DefineSlot(Proto("aes", SlotAlgorithm::kAes256Gcm, kSlotEncrypt)).HasValue());
  EXPECT_TRUE(provider->GetPublicKeyDer("aes").CheckError(CryptoErrc::kIncompatibleArguments));
}

TEST_F(KeySlotTest, ImportPkcs8ChecksAlgorithm) {
  auto k = PrivateKey::Generate(SignatureAlgorithm::kEd25519).Value();
  auto der = k.ToPkcs8Der().Value();
  ASSERT_TRUE(provider->DefineSlot(Proto("ec", SlotAlgorithm::kEcdsaP256, kSlotSign)).HasValue());
  EXPECT_TRUE(provider->Import("ec", der).CheckError(CryptoErrc::kIncompatibleArguments));
  EXPECT_TRUE(provider->Import("ec", Bytes("not der")).CheckError(CryptoErrc::kInvalidArgument));
  ASSERT_TRUE(provider->DefineSlot(Proto("ed", SlotAlgorithm::kEd25519, kSlotSign | kSlotVerify)).HasValue());
  ASSERT_TRUE(provider->Import("ed", der).HasValue());
  auto sig = provider->Sign("ed", Bytes("x")).Value();
  EXPECT_TRUE(k.GetPublicKey().Value().Verify(Bytes("x"), sig).Value()); // same key
}

// ----------------------------------------------------------------- export
TEST_F(KeySlotTest, ExportOnlyWhenExportable) {
  ASSERT_TRUE(provider->DefineSlot(Proto("locked", SlotAlgorithm::kAes256Gcm, kSlotEncrypt)).HasValue());
  ASSERT_TRUE(provider->DefineSlot(Proto("open", SlotAlgorithm::kAes256Gcm, kSlotEncrypt, true)).HasValue());
  ASSERT_TRUE(provider->Import("locked", ByteVector(32, 9)).HasValue());
  ASSERT_TRUE(provider->Import("open", ByteVector(32, 9)).HasValue());
  EXPECT_TRUE(provider->Export("locked").CheckError(CryptoErrc::kKeyNotExportable));
  EXPECT_EQ(provider->Export("open").Value(), ByteVector(32, 9));
}

// ------------------------------------------------------------------- clear
TEST_F(KeySlotTest, ClearEmptiesSlotAndIsIdempotent) {
  ASSERT_TRUE(provider->DefineSlot(Proto("k", SlotAlgorithm::kAes256Gcm, kSlotEncrypt | kSlotDecrypt, false, true)).HasValue());
  ASSERT_TRUE(provider->Generate("k").HasValue());
  EXPECT_TRUE(kvs->KeyExists("keyslot/k").Value());
  ASSERT_TRUE(provider->Clear("k").HasValue());
  EXPECT_EQ(provider->GetSlotInfo("k").Value().state, SlotState::kEmpty);
  EXPECT_FALSE(kvs->KeyExists("keyslot/k").Value());
  EXPECT_TRUE(provider->Seal("k", Bytes(""), Bytes("x")).CheckError(CryptoErrc::kEmptyContainer));
  EXPECT_TRUE(provider->Clear("k").HasValue());
  EXPECT_TRUE(provider->Clear("unknown").CheckError(CryptoErrc::kUnknownIdentifier));
}

TEST_F(KeySlotTest, ClearedKeyStaysGoneAfterRestart) {
  auto const p = Proto("k", SlotAlgorithm::kAes256Gcm, kSlotEncrypt | kSlotDecrypt, false, true);
  ASSERT_TRUE(provider->DefineSlot(p).HasValue());
  ASSERT_TRUE(provider->Generate("k").HasValue());
  ASSERT_TRUE(provider->Clear("k").HasValue());
  Boot();
  ASSERT_TRUE(provider->DefineSlot(p).HasValue());
  EXPECT_EQ(provider->GetSlotInfo("k").Value().state, SlotState::kEmpty);
}

// -------------------------------------------------------------- persistence
TEST_F(KeySlotTest, SymmetricKeySurvivesRestart) {
  auto const p = Proto("aes", SlotAlgorithm::kAes256Gcm, kSlotEncrypt | kSlotDecrypt, false, true);
  ASSERT_TRUE(provider->DefineSlot(p).HasValue());
  ASSERT_TRUE(provider->Generate("aes").HasValue());
  auto sealed = provider->Seal("aes", Bytes("a"), Bytes("persisted")).Value();
  Boot();
  ASSERT_TRUE(provider->DefineSlot(p).HasValue());
  EXPECT_EQ(provider->GetSlotInfo("aes").Value().state, SlotState::kOccupied);
  EXPECT_EQ(provider->Open("aes", Bytes("a"), sealed).Value(), Bytes("persisted"));
}

TEST_F(KeySlotTest, SigningKeySurvivesRestart) {
  auto const p = Proto("ec", SlotAlgorithm::kEcdsaP256, kSlotSign, false, true);
  ASSERT_TRUE(provider->DefineSlot(p).HasValue());
  ASSERT_TRUE(provider->Generate("ec").HasValue());
  auto pubBefore = provider->GetPublicKeyDer("ec").Value();
  Boot();
  ASSERT_TRUE(provider->DefineSlot(p).HasValue());
  EXPECT_EQ(provider->GetPublicKeyDer("ec").Value(), pubBefore);
  auto sig = provider->Sign("ec", Bytes("m")).Value();
  EXPECT_TRUE(PublicKey::FromDer(pubBefore).Value().Verify(Bytes("m"), sig).Value());
}

TEST_F(KeySlotTest, VolatileSlotIsNotPersisted) {
  ASSERT_TRUE(provider->DefineSlot(Proto("tmp", SlotAlgorithm::kAes256Gcm, kSlotEncrypt)).HasValue());
  ASSERT_TRUE(provider->Generate("tmp").HasValue());
  EXPECT_FALSE(kvs->KeyExists("keyslot/tmp").Value());
}

TEST_F(KeySlotTest, StoredBlobIsEncrypted) {
  ASSERT_TRUE(provider->DefineSlot(Proto("aes", SlotAlgorithm::kAes256Gcm, kSlotEncrypt | kSlotDecrypt, true, true)).HasValue());
  ByteVector const material(32, 0xAB);
  ASSERT_TRUE(provider->Import("aes", material).HasValue());
  auto const hex = kvs->GetValue<std::string>("keyslot/aes").Value();
  EXPECT_EQ(hex.find("abababab"), std::string::npos); // plaintext not visible
  EXPECT_EQ(hex.size(), (12 + 32 + 16) * 2U);
}

TEST_F(KeySlotTest, WrongWrappingKeyMarksSlotCorrupted) {
  auto const p = Proto("aes", SlotAlgorithm::kAes256Gcm, kSlotEncrypt | kSlotDecrypt, false, true);
  ASSERT_TRUE(provider->DefineSlot(p).HasValue());
  ASSERT_TRUE(provider->Generate("aes").HasValue());
  wrapKey = GenerateRandom(32).Value();
  Boot();
  ASSERT_TRUE(provider->DefineSlot(p).HasValue());
  EXPECT_EQ(provider->GetSlotInfo("aes").Value().state, SlotState::kCorrupted);
  EXPECT_TRUE(provider->Seal("aes", Bytes(""), Bytes("x")).CheckError(CryptoErrc::kAuthTagNotValid));
}

// --------------------------------------------------------------- tampering
TEST_F(KeySlotTest, FlippedBlobByteIsDetectedAndRepairable) {
  auto const p = Proto("aes", SlotAlgorithm::kAes256Gcm, kSlotEncrypt | kSlotDecrypt, false, true);
  ASSERT_TRUE(provider->DefineSlot(p).HasValue());
  ASSERT_TRUE(provider->Generate("aes").HasValue());
  auto hex = kvs->GetValue<std::string>("keyslot/aes").Value();
  hex[30] = hex[30] == '0' ? '1' : '0';
  ASSERT_TRUE(kvs->SetValue("keyslot/aes", hex).HasValue());
  ASSERT_TRUE(kvs->SyncToStorage().HasValue());
  Boot();
  ASSERT_TRUE(provider->DefineSlot(p).HasValue());
  EXPECT_EQ(provider->GetSlotInfo("aes").Value().state, SlotState::kCorrupted);
  EXPECT_TRUE(provider->Open("aes", Bytes(""), ByteVector(40, 0)).CheckError(CryptoErrc::kAuthTagNotValid));
  // Generating a new key repairs the slot.
  ASSERT_TRUE(provider->Generate("aes").HasValue());
  EXPECT_EQ(provider->GetSlotInfo("aes").Value().state, SlotState::kOccupied);
  Boot();
  ASSERT_TRUE(provider->DefineSlot(p).HasValue());
  EXPECT_EQ(provider->GetSlotInfo("aes").Value().state, SlotState::kOccupied);
}

TEST_F(KeySlotTest, GarbageBlobIsCorrupted) {
  auto const p = Proto("aes", SlotAlgorithm::kAes256Gcm, kSlotEncrypt | kSlotDecrypt, false, true);
  ASSERT_TRUE(kvs->SetValue("keyslot/aes", std::string("zz-not-hex")).HasValue());
  ASSERT_TRUE(provider->DefineSlot(p).HasValue());
  EXPECT_EQ(provider->GetSlotInfo("aes").Value().state, SlotState::kCorrupted);
}

TEST_F(KeySlotTest, BlobCannotBeMovedToAnotherSlot) {
  auto const a = Proto("a", SlotAlgorithm::kAes256Gcm, kSlotEncrypt | kSlotDecrypt, false, true);
  auto const b = Proto("b", SlotAlgorithm::kAes256Gcm, kSlotEncrypt | kSlotDecrypt, false, true);
  ASSERT_TRUE(provider->DefineSlot(a).HasValue());
  ASSERT_TRUE(provider->Generate("a").HasValue());
  ASSERT_TRUE(kvs->SetValue("keyslot/b", kvs->GetValue<std::string>("keyslot/a").Value()).HasValue());
  ASSERT_TRUE(provider->DefineSlot(b).HasValue());
  EXPECT_EQ(provider->GetSlotInfo("b").Value().state, SlotState::kCorrupted);
}

TEST_F(KeySlotTest, PolicyChangeAfterStorageIsDetected) {
  ASSERT_TRUE(provider->DefineSlot(Proto("k", SlotAlgorithm::kAes256Gcm, kSlotEncrypt, false, true)).HasValue());
  ASSERT_TRUE(provider->Generate("k").HasValue());
  Boot();
  // Attacker/misconfiguration widens the usage to include decrypt: blob no longer authenticates.
  ASSERT_TRUE(provider->DefineSlot(Proto("k", SlotAlgorithm::kAes256Gcm, kSlotEncrypt | kSlotDecrypt, false, true)).HasValue());
  EXPECT_EQ(provider->GetSlotInfo("k").Value().state, SlotState::kCorrupted);
}

// ------------------------------------------------------------- concurrency
TEST_F(KeySlotTest, ConcurrentUseIsSafe) {
  ASSERT_TRUE(provider->DefineSlot(Proto("aes", SlotAlgorithm::kAes256Gcm, kSlotEncrypt | kSlotDecrypt)).HasValue());
  ASSERT_TRUE(provider->Generate("aes").HasValue());
  std::atomic<int> ok{0};
  std::vector<std::thread> ts;
  for (int t = 0; t < 4; ++t) {
    ts.emplace_back([&] {
      for (int i = 0; i < 50; ++i) {
        auto s = provider->Seal("aes", Bytes("a"), Bytes("data"));
        if (s.HasValue() && provider->Open("aes", Bytes("a"), s.Value()).HasValue()) ++ok;
      }
    });
  }
  for (auto &t : ts) t.join();
  EXPECT_EQ(ok.load(), 200);
}
