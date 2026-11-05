#include <gtest/gtest.h>

#include "ara/crypto/provider.h"

using namespace ara::crypto;
using namespace ara::crypto::cryp;

namespace {

ByteVector Bytes(std::string const &s) { return ByteVector(s.begin(), s.end()); }

struct ProviderTest : ::testing::Test {
  std::shared_ptr<CryptoProvider> p{LoadCryptoProvider().Value()};
};

TEST_F(ProviderTest, LoadReturnsSharedProvider) {
  EXPECT_EQ(LoadCryptoProvider().Value().get(), p.get());
  EXPECT_STREQ(p->GetProviderName(), "ap-openssl");
}

TEST_F(ProviderTest, HashCtxMatchesKnownVectorAndIsRestartable) {
  auto ctx = p->CreateHashFunctionCtx(HashAlgorithm::kSha256).Value();
  EXPECT_EQ(ctx->GetDigestSize(), 32U);
  EXPECT_FALSE(ctx->Update(Bytes("abc")).HasValue()); // not started
  ASSERT_TRUE(ctx->Start().HasValue());
  ASSERT_TRUE(ctx->Update(Bytes("abc")).HasValue());
  auto d = ctx->Finish().Value();
  ASSERT_EQ(d.size(), 32U);
  EXPECT_EQ(d[0], 0xBA);
  EXPECT_EQ(d[1], 0x78);
  EXPECT_FALSE(ctx->IsStarted()); // Finish ends the operation
  EXPECT_FALSE(ctx->Finish().HasValue());
  ASSERT_TRUE(ctx->Start().HasValue()); // restart
  EXPECT_EQ(ctx->Finish().Value().size(), 32U);
}

TEST_F(ProviderTest, MacCtxLifecycleAndUsageEnforcement) {
  auto bad = SecretKey::Generate(32, kUsageEncrypt).Value();
  auto good = SecretKey::Generate(32, kUsageMac).Value();
  auto ctx = p->CreateMessageAuthnCodeCtx(HashAlgorithm::kSha256).Value();
  EXPECT_EQ(ctx->Start().Error(), MakeErrorCode(CryptoErrc::kUninitializedContext));
  EXPECT_EQ(ctx->SetKey(bad).Error(), MakeErrorCode(CryptoErrc::kUsageViolation));
  ASSERT_TRUE(ctx->SetKey(good).HasValue());
  EXPECT_EQ(ctx->Update(Bytes("x")).Error(), MakeErrorCode(CryptoErrc::kProcessingNotStarted));
  ASSERT_TRUE(ctx->Start().HasValue());
  ASSERT_TRUE(ctx->Update(Bytes("hello")).HasValue());
  auto tag = ctx->Finish().Value();
  EXPECT_EQ(tag.size(), 32U);
  EXPECT_FALSE(ctx->IsKeySet()); // must re-key

  ASSERT_TRUE(ctx->SetKey(good).HasValue());
  ASSERT_TRUE(ctx->Start().HasValue());
  ASSERT_TRUE(ctx->Update(Bytes("hello")).HasValue());
  EXPECT_TRUE(ctx->Check(tag).Value());
  ASSERT_TRUE(ctx->SetKey(good).HasValue());
  ASSERT_TRUE(ctx->Start().HasValue());
  ASSERT_TRUE(ctx->Update(Bytes("hellp")).HasValue());
  EXPECT_FALSE(ctx->Check(tag).Value());
}

TEST_F(ProviderTest, SignVerifyRoundTripAndAlgorithmMismatch) {
  auto key = PrivateKey::Generate(SignatureAlgorithm::kEd25519).Value();
  auto pub = key.GetPublicKey().Value();
  auto signer = p->CreateSignerPrivateCtx(SignatureAlgorithm::kEd25519).Value();
  auto verifier = p->CreateVerifierPublicCtx(SignatureAlgorithm::kEd25519).Value();
  EXPECT_EQ(signer->Sign(Bytes("m")).Error(), MakeErrorCode(CryptoErrc::kUninitializedContext));
  EXPECT_EQ(verifier->Verify(Bytes("m"), Bytes("s")).Error(), MakeErrorCode(CryptoErrc::kUninitializedContext));
  ASSERT_TRUE(signer->SetKey(std::move(key)).HasValue());
  ASSERT_TRUE(verifier->SetKey(std::move(pub)).HasValue());
  auto sig = signer->Sign(Bytes("message")).Value();
  EXPECT_TRUE(verifier->Verify(Bytes("message"), sig).Value());
  EXPECT_FALSE(verifier->Verify(Bytes("messagf"), sig).Value());

  auto ec = PrivateKey::Generate(SignatureAlgorithm::kEcdsaP256Sha256).Value();
  EXPECT_EQ(signer->SetKey(std::move(ec)).Error(), MakeErrorCode(CryptoErrc::kIncompatibleArguments));
}

TEST_F(ProviderTest, RandomGeneratorCtx) {
  auto r = p->CreateRandomGeneratorCtx().Value();
  auto a = r->Generate(32).Value();
  auto b = r->Generate(32).Value();
  EXPECT_EQ(a.size(), 32U);
  EXPECT_NE(a, b);
}

TEST_F(ProviderTest, UnsupportedAlgorithmsAreRejected) {
  EXPECT_EQ(p->CreateHashFunctionCtx(static_cast<HashAlgorithm>(99)).Error(), MakeErrorCode(CryptoErrc::kUnsupported));
  EXPECT_EQ(p->CreateMessageAuthnCodeCtx(static_cast<HashAlgorithm>(99)).Error(), MakeErrorCode(CryptoErrc::kUnsupported));
  EXPECT_EQ(p->CreateSignerPrivateCtx(static_cast<SignatureAlgorithm>(99)).Error(), MakeErrorCode(CryptoErrc::kUnsupported));
  EXPECT_EQ(p->CreateVerifierPublicCtx(static_cast<SignatureAlgorithm>(99)).Error(), MakeErrorCode(CryptoErrc::kUnsupported));
}

TEST(CryptoErrorDomain, BusyResourceHasMessage) {
  EXPECT_STREQ(GetCryptoDomain().Message(static_cast<ara::core::ErrorDomain::CodeType>(CryptoErrc::kBusyResource)),
               "resource is busy");
}

} // namespace
