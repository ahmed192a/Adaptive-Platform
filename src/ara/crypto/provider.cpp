#include "ara/crypto/provider.h"

namespace ara::crypto::cryp {
namespace {
template <typename T> Res<T> Fail(CryptoErrc e) { return Res<T>::FromError(MakeErrorCode(e)); }
bool Valid(HashAlgorithm a) {
  return a == HashAlgorithm::kSha256 || a == HashAlgorithm::kSha384 || a == HashAlgorithm::kSha512;
}
bool Valid(SignatureAlgorithm a) {
  return a == SignatureAlgorithm::kEd25519 || a == SignatureAlgorithm::kEcdsaP256Sha256;
}
} // namespace

Res<void> HashFunctionCtx::Start() {
  auto h = HashFunction::Create(alg_);
  if (!h.HasValue()) return Res<void>::FromError(h.Error());
  h_.emplace(std::move(h).Value());
  return {};
}
Res<void> HashFunctionCtx::Update(ByteView in) {
  if (!h_) return Fail<void>(CryptoErrc::kProcessingNotStarted);
  return h_->Update(in);
}
Res<ByteVector> HashFunctionCtx::Finish() {
  if (!h_) return Fail<ByteVector>(CryptoErrc::kProcessingNotStarted);
  auto r = h_->Finish();
  h_.reset();
  return r;
}

Res<void> MessageAuthnCodeCtx::SetKey(SecretKey const &key) {
  if ((key.Usage() & kUsageMac) == 0) return Fail<void>(CryptoErrc::kUsageViolation);
  auto m = MessageAuthnCode::Create(alg_, key);
  if (!m.HasValue()) return Res<void>::FromError(m.Error());
  m_.emplace(std::move(m).Value());
  started_ = false;
  return {};
}
Res<void> MessageAuthnCodeCtx::Start() {
  if (!m_) return Fail<void>(CryptoErrc::kUninitializedContext);
  started_ = true;
  return {};
}
Res<void> MessageAuthnCodeCtx::Update(ByteView in) {
  if (!m_) return Fail<void>(CryptoErrc::kUninitializedContext);
  if (!started_) return Fail<void>(CryptoErrc::kProcessingNotStarted);
  return m_->Update(in);
}
Res<ByteVector> MessageAuthnCodeCtx::Finish() {
  if (!m_) return Fail<ByteVector>(CryptoErrc::kUninitializedContext);
  if (!started_) return Fail<ByteVector>(CryptoErrc::kProcessingNotStarted);
  auto r = m_->Finish();
  m_.reset(); // key must be set again
  started_ = false;
  return r;
}
Res<bool> MessageAuthnCodeCtx::Check(ByteView expectedTag) {
  auto tag = Finish();
  if (!tag.HasValue()) return Res<bool>::FromError(tag.Error());
  return Res<bool>::FromValue(ConstantTimeEquals(tag.Value(), expectedTag));
}

Res<void> SignerPrivateCtx::SetKey(PrivateKey key) {
  if (key.Algorithm() != alg_) return Fail<void>(CryptoErrc::kIncompatibleArguments);
  key_.emplace(std::move(key));
  return {};
}
Res<ByteVector> SignerPrivateCtx::Sign(ByteView message) const {
  if (!key_) return Fail<ByteVector>(CryptoErrc::kUninitializedContext);
  return key_->Sign(message);
}

Res<void> VerifierPublicCtx::SetKey(PublicKey key) {
  if (key.Algorithm() != alg_) return Fail<void>(CryptoErrc::kIncompatibleArguments);
  key_.emplace(std::move(key));
  return {};
}
Res<bool> VerifierPublicCtx::Verify(ByteView message, ByteView signature) const {
  if (!key_) return Fail<bool>(CryptoErrc::kUninitializedContext);
  return key_->Verify(message, signature);
}

Res<ByteVector> RandomGeneratorCtx::Generate(std::size_t count) const { return GenerateRandom(count); }

Res<std::unique_ptr<HashFunctionCtx>> CryptoProvider::CreateHashFunctionCtx(HashAlgorithm alg) const {
  if (!Valid(alg)) return Fail<std::unique_ptr<HashFunctionCtx>>(CryptoErrc::kUnsupported);
  return Res<std::unique_ptr<HashFunctionCtx>>::FromValue(std::make_unique<HashFunctionCtx>(alg));
}
Res<std::unique_ptr<MessageAuthnCodeCtx>> CryptoProvider::CreateMessageAuthnCodeCtx(HashAlgorithm alg) const {
  if (!Valid(alg)) return Fail<std::unique_ptr<MessageAuthnCodeCtx>>(CryptoErrc::kUnsupported);
  return Res<std::unique_ptr<MessageAuthnCodeCtx>>::FromValue(std::make_unique<MessageAuthnCodeCtx>(alg));
}
Res<std::unique_ptr<SignerPrivateCtx>> CryptoProvider::CreateSignerPrivateCtx(SignatureAlgorithm alg) const {
  if (!Valid(alg)) return Fail<std::unique_ptr<SignerPrivateCtx>>(CryptoErrc::kUnsupported);
  return Res<std::unique_ptr<SignerPrivateCtx>>::FromValue(std::make_unique<SignerPrivateCtx>(alg));
}
Res<std::unique_ptr<VerifierPublicCtx>> CryptoProvider::CreateVerifierPublicCtx(SignatureAlgorithm alg) const {
  if (!Valid(alg)) return Fail<std::unique_ptr<VerifierPublicCtx>>(CryptoErrc::kUnsupported);
  return Res<std::unique_ptr<VerifierPublicCtx>>::FromValue(std::make_unique<VerifierPublicCtx>(alg));
}
Res<std::unique_ptr<RandomGeneratorCtx>> CryptoProvider::CreateRandomGeneratorCtx() const {
  return Res<std::unique_ptr<RandomGeneratorCtx>>::FromValue(std::make_unique<RandomGeneratorCtx>());
}

Res<std::shared_ptr<CryptoProvider>> LoadCryptoProvider() {
  static std::shared_ptr<CryptoProvider> const p = std::make_shared<CryptoProvider>();
  return Res<std::shared_ptr<CryptoProvider>>::FromValue(p);
}

} // namespace ara::crypto::cryp
