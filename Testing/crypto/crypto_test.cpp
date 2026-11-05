#include <gtest/gtest.h>

#include <thread>

#include "ara/crypto/crypto.h"

using namespace ara::crypto;

namespace {

ByteVector FromHex(std::string const &h) {
  ByteVector v;
  for (std::size_t i = 0; i + 1 < h.size(); i += 2)
    v.push_back(static_cast<std::uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16)));
  return v;
}
std::string ToHex(ByteVector const &v) {
  static char const *d = "0123456789abcdef";
  std::string s;
  for (auto b : v) {
    s += d[b >> 4];
    s += d[b & 0xF];
  }
  return s;
}
ByteVector Bytes(std::string const &s) { return ByteVector(s.begin(), s.end()); }

TEST(Random, LengthsAndUniqueness) {
  auto a = GenerateRandom(32);
  auto b = GenerateRandom(32);
  ASSERT_TRUE(a.HasValue() && b.HasValue());
  EXPECT_EQ(a.Value().size(), 32U);
  EXPECT_NE(a.Value(), b.Value());
  EXPECT_TRUE(GenerateRandom(0).Value().empty());
}

TEST(Hash, KnownAnswerSha256AndLengths) {
  EXPECT_EQ(ToHex(Hash(HashAlgorithm::kSha256, ByteView(Bytes("abc"))).Value()),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(Hash(HashAlgorithm::kSha384, ByteView(Bytes("abc"))).Value().size(), 48U);
  EXPECT_EQ(Hash(HashAlgorithm::kSha512, ByteView(Bytes("abc"))).Value().size(), 64U);
  EXPECT_EQ(DigestSize(HashAlgorithm::kSha512), 64U);
}

TEST(Hash, IncrementalMatchesOneShotAndContextIsReusable) {
  auto h = HashFunction::Create(HashAlgorithm::kSha256);
  ASSERT_TRUE(h.HasValue());
  auto &hf = h.Value();
  ASSERT_TRUE(hf.Update(ByteView(Bytes("a"))).HasValue());
  ASSERT_TRUE(hf.Update(ByteView(Bytes("bc"))).HasValue());
  auto d1 = hf.Finish().Value();
  EXPECT_EQ(d1, Hash(HashAlgorithm::kSha256, ByteView(Bytes("abc"))).Value());
  ASSERT_TRUE(hf.Update(ByteView(Bytes("abc"))).HasValue());
  EXPECT_EQ(hf.Finish().Value(), d1);
  EXPECT_EQ(ToHex(hf.Finish().Value()), // empty message
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(SecretKeyTest, ImportValidationAndExportPolicy) {
  EXPECT_EQ(SecretKey::Import(ByteView(), kUsageAll).Error(), MakeErrorCode(CryptoErrc::kEmptyContainer));
  ByteVector k(16, 1);
  EXPECT_EQ(SecretKey::Import(k, 0).Error(), MakeErrorCode(CryptoErrc::kInvalidArgument));
  EXPECT_EQ(SecretKey::Import(k, 0x100).Error(), MakeErrorCode(CryptoErrc::kInvalidArgument));
  auto locked = SecretKey::Import(k, kUsageAll, false);
  ASSERT_TRUE(locked.HasValue());
  EXPECT_EQ(locked.Value().Export().Error(), MakeErrorCode(CryptoErrc::kKeyNotExportable));
  auto open = SecretKey::Import(k, kUsageAll, true);
  EXPECT_EQ(open.Value().Export().Value(), k);
  auto moved = std::move(open).Value();
  EXPECT_TRUE(moved.IsValid());
}

TEST(Hmac, Rfc4231Case1) {
  auto key = SecretKey::Import(ByteVector(20, 0x0b), kUsageMac);
  auto mac = MessageAuthnCode::Create(HashAlgorithm::kSha256, key.Value());
  ASSERT_TRUE(mac.HasValue());
  ASSERT_TRUE(mac.Value().Update(ByteView(Bytes("Hi There"))).HasValue());
  EXPECT_EQ(ToHex(mac.Value().Finish().Value()),
            "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
  // context re-armed with the same key
  auto ok = mac.Value().Verify(ByteView(Bytes("Hi There")),
                               FromHex("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
  EXPECT_TRUE(ok.Value());
  auto bad = mac.Value().Verify(ByteView(Bytes("Hi there")),
                                FromHex("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
  EXPECT_FALSE(bad.Value());
}

TEST(Hmac, UsageIsEnforced) {
  auto key = SecretKey::Import(ByteVector(16, 1), kUsageEncrypt);
  EXPECT_EQ(MessageAuthnCode::Create(HashAlgorithm::kSha256, key.Value()).Error(),
            MakeErrorCode(CryptoErrc::kUsageViolation));
}

TEST(Aead, NistGcmVectors) {
  auto key = SecretKey::Import(ByteVector(16, 0), kUsageEncrypt | kUsageDecrypt);
  ByteVector nonce(12, 0);
  auto empty = AeadEncrypt(key.Value(), nonce, ByteView(), ByteView());
  EXPECT_EQ(ToHex(empty.Value()), "58e2fccefa7e3061367f1d57a4e7455a");
  auto one = AeadEncrypt(key.Value(), nonce, ByteView(), ByteVector(16, 0));
  EXPECT_EQ(ToHex(one.Value()), "0388dace60b6a392f328c2b971b2fe78ab6e47d42cec13bdf53a67b21257bddf");
  EXPECT_EQ(AeadDecrypt(key.Value(), nonce, ByteView(), one.Value()).Value(), ByteVector(16, 0));
}

TEST(Aead, SealOpenRoundTripAndTamperDetection) {
  auto key = SecretKey::Generate(32, kUsageEncrypt | kUsageDecrypt);
  ASSERT_TRUE(key.HasValue());
  auto msg = Bytes("attack at dawn");
  auto aad = Bytes("hdr");
  auto sealed = AeadSeal(key.Value(), aad, msg);
  ASSERT_TRUE(sealed.HasValue());
  EXPECT_EQ(sealed.Value().size(), 12 + msg.size() + 16);
  EXPECT_EQ(AeadOpen(key.Value(), aad, sealed.Value()).Value(), msg);
  EXPECT_NE(AeadSeal(key.Value(), aad, msg).Value(), sealed.Value()); // fresh nonce

  for (std::size_t i : {std::size_t{0}, std::size_t{13}, sealed.Value().size() - 1}) {
    auto t = sealed.Value();
    t[i] ^= 1;
    EXPECT_EQ(AeadOpen(key.Value(), aad, t).Error(), MakeErrorCode(CryptoErrc::kAuthTagNotValid)) << i;
  }
  EXPECT_EQ(AeadOpen(key.Value(), Bytes("other"), sealed.Value()).Error(),
            MakeErrorCode(CryptoErrc::kAuthTagNotValid));
  EXPECT_EQ(AeadOpen(key.Value(), aad, ByteVector(10)).Error(), MakeErrorCode(CryptoErrc::kInvalidInputSize));
}

TEST(Aead, ArgumentAndUsageChecks) {
  auto enc = SecretKey::Import(ByteVector(16, 1), kUsageEncrypt);
  auto bad_len = SecretKey::Import(ByteVector(20, 1), kUsageAll);
  ByteVector nonce(12, 0);
  EXPECT_EQ(AeadDecrypt(enc.Value(), nonce, ByteView(), ByteVector(16)).Error(),
            MakeErrorCode(CryptoErrc::kUsageViolation));
  EXPECT_EQ(AeadEncrypt(bad_len.Value(), nonce, ByteView(), ByteView()).Error(),
            MakeErrorCode(CryptoErrc::kInvalidKeyLength));
  EXPECT_EQ(AeadEncrypt(enc.Value(), ByteVector(8), ByteView(), ByteView()).Error(),
            MakeErrorCode(CryptoErrc::kInvalidArgument));
}

class Signatures : public ::testing::TestWithParam<SignatureAlgorithm> {};
TEST_P(Signatures, SignVerifyAndDerRoundTrip) {
  auto priv = PrivateKey::Generate(GetParam());
  ASSERT_TRUE(priv.HasValue());
  auto msg = Bytes("firmware v1.2");
  auto sig = priv.Value().Sign(msg);
  ASSERT_TRUE(sig.HasValue());
  auto pub = priv.Value().GetPublicKey();
  ASSERT_TRUE(pub.HasValue());
  EXPECT_TRUE(pub.Value().Verify(msg, sig.Value()).Value());
  EXPECT_FALSE(pub.Value().Verify(Bytes("firmware v1.3"), sig.Value()).Value());
  auto bad = sig.Value();
  bad[bad.size() / 2] ^= 0x40;
  EXPECT_FALSE(pub.Value().Verify(msg, bad).Value());

  auto der = pub.Value().ToDer().Value();
  auto imported = PublicKey::FromDer(der);
  ASSERT_TRUE(imported.HasValue());
  EXPECT_EQ(imported.Value().Algorithm(), GetParam());
  EXPECT_TRUE(imported.Value().Verify(msg, sig.Value()).Value());
  // another key must not verify
  auto other = PrivateKey::Generate(GetParam()).Value().GetPublicKey().Value();
  EXPECT_FALSE(other.Verify(msg, sig.Value()).Value());
}
INSTANTIATE_TEST_SUITE_P(All, Signatures,
                         ::testing::Values(SignatureAlgorithm::kEd25519,
                                           SignatureAlgorithm::kEcdsaP256Sha256));

TEST(PublicKeyTest, RejectsGarbage) {
  EXPECT_FALSE(PublicKey::FromDer(ByteVector{1, 2, 3}).HasValue());
  EXPECT_FALSE(PublicKey::FromDer(ByteView()).HasValue());
}

TEST(KeyStoreTest, StoreUseEraseAndConcurrency) {
  KeyStore ks;
  EXPECT_EQ(ks.Store("", SecretKey::Generate(16, kUsageAll).Value()).Error(),
            MakeErrorCode(CryptoErrc::kInvalidArgument));
  EXPECT_EQ(ks.Store("a", SecretKey()).Error(), MakeErrorCode(CryptoErrc::kInvalidArgument));
  ASSERT_TRUE(ks.Store("a", SecretKey::Generate(16, kUsageAll).Value()).HasValue());
  EXPECT_TRUE(ks.Contains("a"));
  auto r = ks.With("a", [](SecretKey const &k) { return AeadSeal(k, ByteView(), ByteView()); });
  ASSERT_TRUE(r.HasValue());
  EXPECT_TRUE(r.Value().HasValue());
  EXPECT_EQ(ks.With("nope", [](SecretKey const &) { return 1; }).Error(),
            MakeErrorCode(CryptoErrc::kUnknownIdentifier));

  std::vector<std::thread> ts;
  for (int t = 0; t < 4; ++t)
    ts.emplace_back([&ks, t] {
      for (int i = 0; i < 50; ++i) {
        (void)ks.Store("k" + std::to_string(t), SecretKey::Generate(32, kUsageAll).Value());
        (void)ks.With("a", [](SecretKey const &k) { return k.Size(); });
      }
    });
  for (auto &t : ts) t.join();
  EXPECT_TRUE(ks.Erase("a").HasValue());
  EXPECT_EQ(ks.Erase("a").Error(), MakeErrorCode(CryptoErrc::kUnknownIdentifier));
}

TEST(Util, ConstantTimeEqualsAndErrorDomain) {
  EXPECT_TRUE(ConstantTimeEquals(Bytes("ab"), Bytes("ab")));
  EXPECT_FALSE(ConstantTimeEquals(Bytes("ab"), Bytes("ac")));
  EXPECT_FALSE(ConstantTimeEquals(Bytes("ab"), Bytes("abc")));
  EXPECT_TRUE(ConstantTimeEquals(ByteView(), ByteView()));
  EXPECT_STREQ(MakeErrorCode(CryptoErrc::kAuthTagNotValid).Domain().Name(), "Crypto");
  EXPECT_THROW(MakeErrorCode(CryptoErrc::kRuntimeFault).ThrowAsException(), CryptoException);
}

} // namespace
