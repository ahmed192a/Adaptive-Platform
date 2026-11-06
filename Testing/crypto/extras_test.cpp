#include <gtest/gtest.h>

#include <string>

#include "ara/crypto/extras.h"
#include "ara/crypto/provider.h"

using namespace ara::crypto;

namespace {

ByteVector Hex(std::string const &h) {
  ByteVector out;
  for (std::size_t i = 0; i + 1 < h.size(); i += 2) out.push_back(static_cast<std::uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16)));
  return out;
}
ByteVector Bytes(std::string const &s) { return ByteVector(s.begin(), s.end()); }
template <typename R> bool IsErr(R const &r, CryptoErrc e) {
  return !r.HasValue() && r.Error() == MakeErrorCode(e);
}
SecretKey Key(std::string const &hex, std::uint32_t usage = kUsageEncrypt | kUsageDecrypt, bool exportable = false) {
  return SecretKey::Import(Hex(hex), usage, exportable).Value();
}

// ----------------------------------------------------------------- KDF
TEST(Kdf, HkdfRfc5869TestCase1) {
  auto okm = Hkdf(HashAlgorithm::kSha256, Hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"), Hex("000102030405060708090a0b0c"),
                  Hex("f0f1f2f3f4f5f6f7f8f9"), 42);
  ASSERT_TRUE(okm.HasValue());
  EXPECT_EQ(okm.Value(), Hex("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"));
}

TEST(Kdf, HkdfRejectsBadLengthsAndEmptyIkm) {
  EXPECT_TRUE(IsErr(Hkdf(HashAlgorithm::kSha256, ByteView{}, ByteView{}, ByteView{}, 16), CryptoErrc::kEmptyContainer));
  ByteVector ikm(16, 1);
  EXPECT_TRUE(IsErr(Hkdf(HashAlgorithm::kSha256, ikm, ByteView{}, ByteView{}, 0), CryptoErrc::kInvalidArgument));
  EXPECT_TRUE(IsErr(Hkdf(HashAlgorithm::kSha256, ikm, ByteView{}, ByteView{}, 255 * 32 + 1), CryptoErrc::kInvalidArgument));
}

TEST(Kdf, Pbkdf2KnownAnswers) {
  auto pw = Bytes("password");
  auto salt = Bytes("salt");
  EXPECT_EQ(Pbkdf2(HashAlgorithm::kSha256, pw, salt, 1, 32, true).Value(),
            Hex("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"));
  EXPECT_EQ(Pbkdf2(HashAlgorithm::kSha256, pw, salt, 2, 32, true).Value(),
            Hex("ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43"));
  EXPECT_EQ(Pbkdf2(HashAlgorithm::kSha256, pw, salt, 4096, 32, true).Value(),
            Hex("c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"));
  EXPECT_TRUE(IsErr(Pbkdf2(HashAlgorithm::kSha256, pw, salt, 0, 32), CryptoErrc::kInvalidArgument));
  // defaults enforce sane minimums: the 4-byte salt / 1 iteration vector is refused
  EXPECT_TRUE(IsErr(Pbkdf2(HashAlgorithm::kSha256, pw, salt, 1, 32), CryptoErrc::kRuntimeFault));
  EXPECT_TRUE(Pbkdf2(HashAlgorithm::kSha256, pw, Bytes("0123456789abcdef"), 1000, 32).HasValue());
  EXPECT_TRUE(IsErr(Pbkdf2(HashAlgorithm::kSha256, ByteView{}, salt, 1, 32), CryptoErrc::kEmptyContainer));
}

TEST(Kdf, DeriveKeyIsDeterministicAndKeepsUsage) {
  ByteVector ikm(32, 7);
  auto a = HkdfDeriveKey(HashAlgorithm::kSha256, ikm, Bytes("s"), Bytes("ctx"), 32, kUsageMac, true).Value();
  auto b = HkdfDeriveKey(HashAlgorithm::kSha256, ikm, Bytes("s"), Bytes("ctx"), 32, kUsageMac, true).Value();
  auto c = HkdfDeriveKey(HashAlgorithm::kSha256, ikm, Bytes("s"), Bytes("other"), 32, kUsageMac, true).Value();
  EXPECT_EQ(a.Export().Value(), b.Export().Value());
  EXPECT_NE(a.Export().Value(), c.Export().Value());
  EXPECT_EQ(a.Usage(), static_cast<std::uint32_t>(kUsageMac));
}

// ------------------------------------------------------------- CBC / CTR
TEST(Cipher, CbcMatchesNistSp80038aBlock1) {
  auto key = Key("2b7e151628aed2a6abf7158809cf4f3c");
  auto ct = CipherEncrypt(CipherMode::kCbc, key, Hex("000102030405060708090a0b0c0d0e0f"),
                          Hex("6bc1bee22e409f96e93d7e117393172a"), /*pkcs7Padding=*/false);
  ASSERT_TRUE(ct.HasValue());
  EXPECT_EQ(ct.Value(), Hex("7649abac8119b246cee98e9b12e9197d"));
}

TEST(Cipher, CtrMatchesNistSp80038aBlock1) {
  auto key = Key("2b7e151628aed2a6abf7158809cf4f3c");
  auto ct = CipherEncrypt(CipherMode::kCtr, key, Hex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"),
                          Hex("6bc1bee22e409f96e93d7e117393172a"));
  ASSERT_TRUE(ct.HasValue());
  EXPECT_EQ(ct.Value(), Hex("874d6191b620e3261bef6864990db6ce"));
}

TEST(Cipher, CbcPaddedRoundTripAllLengths) {
  auto key = SecretKey::Generate(32, kUsageEncrypt | kUsageDecrypt).Value();
  ByteVector iv(16, 9);
  for (std::size_t n : {0U, 1U, 15U, 16U, 17U, 100U}) {
    ByteVector pt(n, 0x5A);
    auto ct = CipherEncrypt(CipherMode::kCbc, key, iv, pt).Value();
    EXPECT_EQ(ct.size() % 16, 0U);
    EXPECT_GT(ct.size(), n);
    EXPECT_EQ(CipherDecrypt(CipherMode::kCbc, key, iv, ct).Value(), pt) << n;
  }
}

TEST(Cipher, CtrAnyLengthKeepsSizeAndRoundTrips) {
  auto key = SecretKey::Generate(24, kUsageEncrypt | kUsageDecrypt).Value();
  ByteVector iv(16, 1);
  ByteVector pt = Bytes("not a multiple of sixteen!");
  auto ct = CipherEncrypt(CipherMode::kCtr, key, iv, pt).Value();
  EXPECT_EQ(ct.size(), pt.size());
  EXPECT_EQ(CipherDecrypt(CipherMode::kCtr, key, iv, ct).Value(), pt);
}

TEST(Cipher, EnforcesUsageKeySizeAndIvSize) {
  ByteVector iv(16, 0);
  auto encOnly = SecretKey::Generate(16, kUsageEncrypt).Value();
  EXPECT_TRUE(IsErr(CipherDecrypt(CipherMode::kCtr, encOnly, iv, Bytes("x")), CryptoErrc::kUsageViolation));
  auto decOnly = SecretKey::Generate(16, kUsageDecrypt).Value();
  EXPECT_TRUE(IsErr(CipherEncrypt(CipherMode::kCtr, decOnly, iv, Bytes("x")), CryptoErrc::kUsageViolation));
  auto odd = SecretKey::Generate(20, kUsageEncrypt).Value();
  EXPECT_TRUE(IsErr(CipherEncrypt(CipherMode::kCtr, odd, iv, Bytes("x")), CryptoErrc::kInvalidKeyLength));
  EXPECT_TRUE(IsErr(CipherEncrypt(CipherMode::kCtr, encOnly, ByteVector(12, 0), Bytes("x")), CryptoErrc::kInvalidInputSize));
  EXPECT_TRUE(IsErr(CipherEncrypt(CipherMode::kCbc, encOnly, iv, Bytes("x"), false), CryptoErrc::kInvalidInputSize));
}

TEST(Cipher, CbcWrongKeyOrCorruptPaddingFails) {
  auto key = SecretKey::Generate(16, kUsageEncrypt | kUsageDecrypt).Value();
  auto other = SecretKey::Generate(16, kUsageEncrypt | kUsageDecrypt).Value();
  ByteVector iv(16, 3);
  auto ct = CipherEncrypt(CipherMode::kCbc, key, iv, Bytes("secret message")).Value();
  auto bad = CipherDecrypt(CipherMode::kCbc, other, iv, ct);
  // wrong key almost always yields bad padding; if padding happens to parse the plaintext must differ
  if (bad.HasValue()) EXPECT_NE(bad.Value(), Bytes("secret message"));
  else EXPECT_EQ(bad.Error(), MakeErrorCode(CryptoErrc::kAuthTagNotValid));
  EXPECT_TRUE(IsErr(CipherDecrypt(CipherMode::kCbc, key, iv, ByteVector(15, 1)), CryptoErrc::kInvalidInputSize));
}

// ---------------------------------------------------------------- key wrap
TEST(KeyWrap, Rfc3394Vectors) {
  auto kek128 = Key("000102030405060708090A0B0C0D0E0F");
  auto data = Key("00112233445566778899AABBCCDDEEFF", kUsageEncrypt, true);
  EXPECT_EQ(WrapKey(kek128, data).Value(), Hex("1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5"));
  auto kek256 = Key("000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F");
  auto data256 = Key("00112233445566778899AABBCCDDEEFF000102030405060708090A0B0C0D0E0F", kUsageEncrypt, true);
  EXPECT_EQ(WrapKey(kek256, data256).Value(),
            Hex("28c9f404c4b810f4cbccb35cfb87f8263f5786e2d80ed326cbc7f0e71a99f43bfb988b9b7a02dd21"));
}

TEST(KeyWrap, UnwrapRoundTripAndTamperDetection) {
  auto kek = SecretKey::Generate(32, kUsageEncrypt | kUsageDecrypt).Value();
  auto target = SecretKey::Generate(32, kUsageMac, true).Value();
  auto wrapped = WrapKey(kek, target).Value();
  EXPECT_EQ(wrapped.size(), 40U);
  auto back = UnwrapKey(kek, wrapped, kUsageMac, true).Value();
  EXPECT_EQ(back.Export().Value(), target.Export().Value());
  wrapped[5] ^= 1;
  EXPECT_TRUE(IsErr(UnwrapKey(kek, wrapped, kUsageMac), CryptoErrc::kAuthTagNotValid));
}

TEST(KeyWrap, Guards) {
  auto kek = SecretKey::Generate(16, kUsageEncrypt | kUsageDecrypt).Value();
  auto hidden = SecretKey::Generate(16, kUsageMac, false).Value();
  EXPECT_TRUE(IsErr(WrapKey(kek, hidden), CryptoErrc::kKeyNotExportable));
  auto onlyDec = SecretKey::Generate(16, kUsageDecrypt).Value();
  auto exp = SecretKey::Generate(16, kUsageMac, true).Value();
  EXPECT_TRUE(IsErr(WrapKey(onlyDec, exp), CryptoErrc::kUsageViolation));
  auto onlyEnc = SecretKey::Generate(16, kUsageEncrypt).Value();
  EXPECT_TRUE(IsErr(UnwrapKey(onlyEnc, ByteVector(24, 0), kUsageMac), CryptoErrc::kUsageViolation));
  auto small = SecretKey::Import(ByteVector(8, 1), kUsageMac, true).Value();
  EXPECT_TRUE(IsErr(WrapKey(kek, small), CryptoErrc::kInvalidInputSize));
}

// -------------------------------------------------------------- SecretSeed
TEST(Seed, XorJumpAndDerive) {
  auto a = SecretSeed::Import(Hex("0f0f")).Value();
  auto b = SecretSeed::Import(Hex("ff00")).Value();
  ASSERT_TRUE(a.XorWith(b).HasValue());
  EXPECT_EQ(a.ToKey(kUsageMac, true).Value().Export().Value(), Hex("f00f"));
  auto c = SecretSeed::Import(Hex("0001")).Value();
  EXPECT_TRUE(IsErr(a.XorWith(SecretSeed::Import(Hex("00")).Value()), CryptoErrc::kInvalidInputSize));
  auto exp = [](SecretSeed &s) { return s.ToKey(kUsageMac, true).Value().Export().Value(); };
  c.Jump(0xFF);                       // 0x0001 + 0xFF = 0x0100
  EXPECT_EQ(exp(c), Hex("0100"));
  c.Next();
  EXPECT_EQ(exp(c), Hex("0101"));
  c.Jump(-2);                         // borrow across the byte
  EXPECT_EQ(exp(c), Hex("00ff"));
  auto w = SecretSeed::Import(Hex("ffff")).Value();
  w.Next();                           // wraps
  EXPECT_EQ(exp(w), Hex("0000"));

  auto s = SecretSeed::Import(Hex("000102030405060708090a0b0c0d0e0f")).Value();
  auto k1 = s.DeriveKey(Bytes("enc"), 16, kUsageEncrypt).Value();
  auto k2 = s.DeriveKey(Bytes("enc"), 16, kUsageEncrypt).Value();
  EXPECT_EQ(k1.Size(), 16U);
  // keys are not exportable: derive again and compare through a deterministic cipher output
  ByteVector iv(16, 0);
  EXPECT_EQ(CipherEncrypt(CipherMode::kCtr, k1, iv, Bytes("probe")).Value(),
            CipherEncrypt(CipherMode::kCtr, k2, iv, Bytes("probe")).Value());
  EXPECT_TRUE(IsErr(SecretSeed::Generate(0), CryptoErrc::kInvalidArgument));
  EXPECT_EQ(SecretSeed::Generate(24).Value().Size(), 24U);
}

// --------------------------------------------------------------------- RSA
RsaPrivateKey const &SharedRsa() {
  static RsaPrivateKey const k = RsaPrivateKey::Generate(2048).Value();
  return k;
}

TEST(Rsa, PssSignVerifyAndTamper) {
  auto pub = SharedRsa().GetPublicKey().Value();
  EXPECT_EQ(pub.ModulusBits(), 2048U);
  auto sig = SharedRsa().SignPss(Bytes("firmware v1")).Value();
  EXPECT_EQ(sig.size(), 256U);
  EXPECT_TRUE(pub.VerifyPss(Bytes("firmware v1"), sig).Value());
  EXPECT_FALSE(pub.VerifyPss(Bytes("firmware v2"), sig).Value());
  sig[10] ^= 1;
  EXPECT_FALSE(pub.VerifyPss(Bytes("firmware v1"), sig).Value());
  EXPECT_NE(SharedRsa().SignPss(Bytes("x")).Value(), SharedRsa().SignPss(Bytes("x")).Value()); // random salt
}

TEST(Rsa, OaepRoundTripAndWrongKey) {
  auto pub = SharedRsa().GetPublicKey().Value();
  auto ct = pub.EncryptOaep(Bytes("session key")).Value();
  EXPECT_EQ(ct.size(), 256U);
  EXPECT_EQ(SharedRsa().DecryptOaep(ct).Value(), Bytes("session key"));
  auto other = RsaPrivateKey::Generate(2048).Value();
  EXPECT_TRUE(IsErr(other.DecryptOaep(ct), CryptoErrc::kAuthTagNotValid));
  EXPECT_TRUE(IsErr(pub.EncryptOaep(ByteVector(300, 1)), CryptoErrc::kInvalidInputSize));
}

TEST(Rsa, DerRoundTripAndRejections) {
  auto der = SharedRsa().ToPkcs8Der().Value();
  auto again = RsaPrivateKey::FromPkcs8Der(der).Value();
  auto sig = again.SignPss(Bytes("m")).Value();
  EXPECT_TRUE(SharedRsa().GetPublicKey().Value().VerifyPss(Bytes("m"), sig).Value());
  SecureZero(der.data(), der.size());
  auto pubDer = SharedRsa().GetPublicKey().Value().ToDer().Value();
  EXPECT_TRUE(RsaPublicKey::FromDer(pubDer).HasValue());
  EXPECT_TRUE(IsErr(RsaPrivateKey::Generate(1024), CryptoErrc::kInvalidArgument));
  EXPECT_TRUE(IsErr(RsaPublicKey::FromDer(Bytes("garbage")), CryptoErrc::kInvalidArgument));
  // an Ed25519 key is valid DER but not RSA
  auto ed = PrivateKey::Generate(SignatureAlgorithm::kEd25519).Value();
  EXPECT_TRUE(IsErr(RsaPublicKey::FromDer(ed.GetPublicKey().Value().ToDer().Value()), CryptoErrc::kUnsupported));
}

// -------------------------------------------------------------------- X.509
struct Pki {
  PrivateKey rootKey{PrivateKey::Generate(SignatureAlgorithm::kEd25519).Value()};
  PrivateKey caKey{PrivateKey::Generate(SignatureAlgorithm::kEcdsaP256Sha256).Value()};
  PrivateKey leafKey{PrivateKey::Generate(SignatureAlgorithm::kEcdsaP256Sha256).Value()};
  X509Certificate root;
  X509Certificate ca;
  X509Certificate leaf;
  Pki()
      : root(IssueCertificate({"Test Root CA", rootKey.GetPublicKey().Value().ToDer().Value(), 1, 0, 0, true}, nullptr,
                              rootKey)
                 .Value()),
        ca(IssueCertificate({"Test Intermediate CA", caKey.GetPublicKey().Value().ToDer().Value(), 2, 0, 0, true}, &root,
                            rootKey)
               .Value()),
        leaf(IssueCertificate({"ecu-1.example", leafKey.GetPublicKey().Value().ToDer().Value(), 3, 0, 0, false}, &ca,
                              caKey)
                 .Value()) {}
};

TEST(X509, ParseFieldsAndEncodings) {
  Pki pki;
  EXPECT_EQ(pki.root.SubjectName(), "CN=Test Root CA");
  EXPECT_EQ(pki.root.IssuerName(), "CN=Test Root CA");
  EXPECT_TRUE(pki.root.IsSelfIssued());
  EXPECT_TRUE(pki.root.IsCa());
  EXPECT_EQ(pki.ca.IssuerName(), "CN=Test Root CA");
  EXPECT_FALSE(pki.ca.IsSelfIssued());
  EXPECT_FALSE(pki.leaf.IsCa());
  EXPECT_EQ(pki.leaf.SerialNumberHex(), "03");
  EXPECT_GT(pki.leaf.NotAfter(), pki.leaf.NotBefore());
  EXPECT_EQ(pki.leaf.Fingerprint().Value().size(), 32U);

  auto fromDer = X509Certificate::FromDer(pki.leaf.ToDer().Value()).Value();
  EXPECT_EQ(fromDer.SubjectName(), "CN=ecu-1.example");
  auto pem = pki.leaf.ToPem().Value();
  EXPECT_NE(pem.find("BEGIN CERTIFICATE"), std::string::npos);
  EXPECT_EQ(X509Certificate::FromPem(pem).Value().Fingerprint().Value(), pki.leaf.Fingerprint().Value());
  EXPECT_TRUE(IsErr(X509Certificate::FromDer(Bytes("not a cert")), CryptoErrc::kInvalidArgument));
  EXPECT_TRUE(IsErr(X509Certificate::FromPem("nope"), CryptoErrc::kInvalidArgument));
}

TEST(X509, IsSignedByChecksIssuerKey) {
  Pki pki;
  EXPECT_TRUE(pki.leaf.IsSignedBy(pki.ca.GetPublicKeyDer().Value()).Value());
  EXPECT_FALSE(pki.leaf.IsSignedBy(pki.root.GetPublicKeyDer().Value()).Value());
  EXPECT_TRUE(pki.root.IsSignedBy(pki.root.GetPublicKeyDer().Value()).Value());
}

TEST(X509, ChainVerifiesToTrustAnchor) {
  Pki pki;
  X509Provider prov;
  EXPECT_EQ(prov.TrustAnchorCount(), 0U);
  EXPECT_TRUE(IsErr(prov.VerifyChain(pki.leaf, {pki.ca}), CryptoErrc::kEmptyContainer));
  ASSERT_TRUE(prov.AddTrustAnchor(pki.root).HasValue());
  EXPECT_EQ(prov.TrustAnchorCount(), 1U);
  EXPECT_TRUE(prov.VerifyChain(pki.leaf, {pki.ca}).HasValue());
  EXPECT_TRUE(prov.VerifyChain(pki.ca).HasValue());
}

TEST(X509, MissingIntermediateOrWrongAnchorFails) {
  Pki pki;
  Pki stranger;
  X509Provider prov;
  ASSERT_TRUE(prov.AddTrustAnchor(pki.root).HasValue());
  auto noInter = prov.VerifyChain(pki.leaf);
  ASSERT_FALSE(noInter.HasValue());
  EXPECT_EQ(noInter.Error().Domain(), GetCryptoDomain());
  EXPECT_NE(noInter.Error().SupportData(), 0); // X509_V_ERR_* code
  X509Provider other;
  ASSERT_TRUE(other.AddTrustAnchor(stranger.root).HasValue());
  EXPECT_FALSE(other.VerifyChain(pki.leaf, {pki.ca}).HasValue());
  prov.ClearTrustAnchors();
  EXPECT_EQ(prov.TrustAnchorCount(), 0U);
}

TEST(X509, ValidityWindowIsEnforced) {
  Pki pki;
  X509Provider prov;
  ASSERT_TRUE(prov.AddTrustAnchor(pki.root).HasValue());
  std::time_t const later = pki.leaf.NotAfter() + 3600;
  std::time_t const earlier = pki.leaf.NotBefore() - 3600;
  EXPECT_FALSE(prov.VerifyChain(pki.leaf, {pki.ca}, later).HasValue());
  EXPECT_FALSE(prov.VerifyChain(pki.leaf, {pki.ca}, earlier).HasValue());
  EXPECT_TRUE(prov.VerifyChain(pki.leaf, {pki.ca}, pki.leaf.NotBefore() + 120).HasValue());
}

TEST(X509, NonCaCannotIssueAndAnchorMustBeCa) {
  Pki pki;
  X509Provider prov;
  EXPECT_TRUE(IsErr(prov.AddTrustAnchor(pki.leaf), CryptoErrc::kInvalidArgument));
  ASSERT_TRUE(prov.AddTrustAnchor(pki.root).HasValue());
  // a leaf (CA:FALSE) that signs another certificate must not validate
  auto badKey = PrivateKey::Generate(SignatureAlgorithm::kEcdsaP256Sha256).Value();
  auto child = IssueCertificate({"child", badKey.GetPublicKey().Value().ToDer().Value(), 9, 0, 0, false}, &pki.leaf,
                                pki.leafKey)
                   .Value();
  EXPECT_FALSE(prov.VerifyChain(child, {pki.leaf, pki.ca}).HasValue());
}

TEST(X509, TamperedCertificateDoesNotVerify) {
  Pki pki;
  X509Provider prov;
  ASSERT_TRUE(prov.AddTrustAnchor(pki.root).HasValue());
  auto der = pki.leaf.ToDer().Value();
  der[der.size() - 3] ^= 1; // inside the signature
  auto parsed = X509Certificate::FromDer(der);
  if (parsed.HasValue()) EXPECT_FALSE(prov.VerifyChain(parsed.Value(), {pki.ca}).HasValue());
}

TEST(X509, IssueRejectsBadRequests) {
  auto k = PrivateKey::Generate(SignatureAlgorithm::kEd25519).Value();
  auto spki = k.GetPublicKey().Value().ToDer().Value();
  EXPECT_TRUE(IsErr(IssueCertificate({"", spki, 1, 0, 0, true}, nullptr, k), CryptoErrc::kInvalidArgument));
  EXPECT_TRUE(IsErr(IssueCertificate({"x", ByteVector{}, 1, 0, 0, true}, nullptr, k), CryptoErrc::kInvalidArgument));
  EXPECT_TRUE(IsErr(IssueCertificate({"x", spki, 1, 2000000000, 1000000000, true}, nullptr, k), CryptoErrc::kInvalidArgument));
}

// ------------------------------------------------------------ SWS facade
TEST(ProviderExt, KdfCtxHkdfAndPbkdf2) {
  auto p = cryp::LoadCryptoProvider().Value();
  auto hk = p->CreateKeyDerivationFunctionCtx(cryp::KdfKind::kHkdf, HashAlgorithm::kSha256).Value();
  EXPECT_TRUE(IsErr(hk->DeriveBytes(Bytes("k"), 16), CryptoErrc::kUninitializedContext));
  ASSERT_TRUE(hk->Configure(Hex("000102030405060708090a0b0c"), Hex("f0f1f2f3f4f5f6f7f8f9")).HasValue());
  EXPECT_EQ(hk->DeriveBytes(Hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"), 42).Value(),
            Hex("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"));
  auto pb = p->CreateKeyDerivationFunctionCtx(cryp::KdfKind::kPbkdf2, HashAlgorithm::kSha256).Value();
  EXPECT_TRUE(IsErr(pb->Configure(Bytes("salt"), ByteView{}, 0), CryptoErrc::kInvalidArgument));
  ASSERT_TRUE(pb->Configure(Bytes("salt"), ByteView{}, 1).HasValue());
  EXPECT_FALSE(pb->DeriveBytes(Bytes("password"), 32).HasValue()); // strict by default
  pb->AllowWeakParameters(true);
  EXPECT_EQ(pb->DeriveBytes(Bytes("password"), 32).Value(),
            Hex("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"));
  EXPECT_EQ(pb->DeriveKey(Bytes("password"), 16, kUsageMac, true).Value().Size(), 16U);
}

TEST(ProviderExt, CipherContexts) {
  auto p = cryp::LoadCryptoProvider().Value();
  auto cbc = p->CreateSymmetricBlockCipherCtx().Value();
  EXPECT_TRUE(IsErr(cbc->Encrypt(ByteVector(16, 0), Bytes("x")), CryptoErrc::kUninitializedContext));
  ASSERT_TRUE(cbc->SetKey(SecretKey::Generate(32, kUsageEncrypt | kUsageDecrypt).Value()).HasValue());
  ByteVector iv(16, 4);
  auto ct = cbc->Encrypt(iv, Bytes("hello cbc")).Value();
  EXPECT_EQ(cbc->Decrypt(iv, ct).Value(), Bytes("hello cbc"));
  auto ctr = p->CreateStreamCipherCtx().Value();
  ASSERT_TRUE(ctr->SetKey(SecretKey::Generate(16, kUsageEncrypt | kUsageDecrypt).Value()).HasValue());
  auto sct = ctr->Encrypt(iv, Bytes("hello ctr")).Value();
  EXPECT_EQ(sct.size(), 9U);
  EXPECT_EQ(ctr->Decrypt(iv, sct).Value(), Bytes("hello ctr"));
  auto gcm = p->CreateAuthCipherCtx().Value();
  ASSERT_TRUE(gcm->SetKey(SecretKey::Generate(32, kUsageEncrypt | kUsageDecrypt).Value()).HasValue());
  ByteVector nonce(12, 2);
  auto sealed = gcm->Encrypt(nonce, Bytes("hdr"), Bytes("body")).Value();
  EXPECT_EQ(gcm->Decrypt(nonce, Bytes("hdr"), sealed).Value(), Bytes("body"));
  EXPECT_TRUE(IsErr(gcm->Decrypt(nonce, Bytes("other"), sealed), CryptoErrc::kAuthTagNotValid));
}

TEST(ProviderExt, WrapperAndRsaContexts) {
  auto p = cryp::LoadCryptoProvider().Value();
  auto w = p->CreateSymmetricKeyWrapperCtx().Value();
  auto payload = SecretKey::Generate(16, kUsageMac, true).Value();
  EXPECT_TRUE(IsErr(w->WrapKey(payload), CryptoErrc::kUninitializedContext));
  ASSERT_TRUE(w->SetKey(SecretKey::Generate(16, kUsageEncrypt | kUsageDecrypt).Value()).HasValue());
  auto blob = w->WrapKey(payload).Value();
  EXPECT_EQ(w->UnwrapKey(blob, kUsageMac, true).Value().Export().Value(), payload.Export().Value());

  auto pub = SharedRsa().GetPublicKey().Value();
  auto enc = p->CreateEncryptorPublicCtx().Value();
  auto dec = p->CreateDecryptorPrivateCtx().Value();
  EXPECT_TRUE(IsErr(enc->Encrypt(Bytes("m")), CryptoErrc::kUninitializedContext));
  ASSERT_TRUE(enc->SetKey(RsaPublicKey::FromDer(pub.ToDer().Value()).Value()).HasValue());
  ASSERT_TRUE(dec->SetKey(RsaPrivateKey::FromPkcs8Der(SharedRsa().ToPkcs8Der().Value()).Value()).HasValue());
  EXPECT_EQ(dec->Decrypt(enc->Encrypt(Bytes("hi")).Value()).Value(), Bytes("hi"));
  auto signer = p->CreateRsaSignerPrivateCtx().Value();
  auto verifier = p->CreateRsaVerifierPublicCtx().Value();
  ASSERT_TRUE(signer->SetKey(RsaPrivateKey::FromPkcs8Der(SharedRsa().ToPkcs8Der().Value()).Value()).HasValue());
  ASSERT_TRUE(verifier->SetKey(RsaPublicKey::FromDer(pub.ToDer().Value()).Value()).HasValue());
  EXPECT_TRUE(verifier->Verify(Bytes("doc"), signer->Sign(Bytes("doc")).Value()).Value());
}

TEST(ProviderExt, UnsupportedFactoriesAndGlobalLoaders) {
  auto p = cryp::LoadCryptoProvider().Value();
  EXPECT_TRUE(IsErr(p->CreateKeyEncapsulatorPublicCtx(), CryptoErrc::kUnsupported));
  EXPECT_TRUE(IsErr(p->CreateSigEncodePrivateCtx(), CryptoErrc::kUnsupported));
  EXPECT_TRUE(IsErr(p->CreateMsgRecoveryPublicCtx(), CryptoErrc::kUnsupported));
  EXPECT_EQ(cryp::LoadX509Provider().Value().get(), cryp::LoadX509Provider().Value().get());
  auto ks = cryp::LoadKeyStorageProvider().Value();
  EXPECT_EQ(ks.get(), cryp::LoadKeyStorageProvider().Value().get());
  EXPECT_STREQ(GetCryptoDomain().Message(static_cast<ara::core::ErrorDomain::CodeType>(CryptoErrc::kAlgIdUndefined)),
               "algorithm id undefined");
}

} // namespace
