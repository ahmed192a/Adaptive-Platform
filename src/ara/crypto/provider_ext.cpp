#include "ara/crypto/provider.h"

namespace ara::crypto::cryp {
namespace {
template <typename T> Res<T> Fail(CryptoErrc e) { return Res<T>::FromError(MakeErrorCode(e)); }
Res<void> FailV(CryptoErrc e) { return Res<void>::FromError(MakeErrorCode(e)); }
template <typename C, typename... A> Res<std::unique_ptr<C>> Make(A &&...a) {
  return Res<std::unique_ptr<C>>::FromValue(std::make_unique<C>(std::forward<A>(a)...));
}
} // namespace

// --------------------------------------------------------------------- KDF
Res<void> KeyDerivationFunctionCtx::Configure(ByteView salt, ByteView info, std::uint32_t iterations) {
  if (kind_ == KdfKind::kPbkdf2 && (iterations == 0 || salt.empty())) return FailV(CryptoErrc::kInvalidArgument);
  salt_.assign(salt.data(), salt.data() + salt.size());
  info_.assign(info.data(), info.data() + info.size());
  iterations_ = iterations;
  configured_ = true;
  return {};
}
Res<ByteVector> KeyDerivationFunctionCtx::DeriveBytes(ByteView secret, std::size_t length) const {
  if (!configured_) return Fail<ByteVector>(CryptoErrc::kUninitializedContext);
  return kind_ == KdfKind::kHkdf ? Hkdf(alg_, secret, salt_, info_, length)
                                 : Pbkdf2(alg_, secret, salt_, iterations_, length, weak_);
}
Res<SecretKey> KeyDerivationFunctionCtx::DeriveKey(ByteView secret, std::size_t keySize, std::uint32_t usage,
                                                   bool exportable) const {
  auto b = DeriveBytes(secret, keySize);
  if (!b.HasValue()) return Res<SecretKey>::FromError(b.Error());
  auto k = SecretKey::Import(b.Value(), usage, exportable);
  SecureZero(b.Value().data(), b.Value().size());
  return k;
}

// ----------------------------------------------------------------- ciphers
Res<void> CipherCtxBase::SetKey(SecretKey key) {
  if (!key.IsValid()) return FailV(CryptoErrc::kEmptyContainer);
  key_.emplace(std::move(key));
  return {};
}
Res<ByteVector> CipherCtxBase::Encrypt(ByteView iv, ByteView in) const {
  if (!key_) return Fail<ByteVector>(CryptoErrc::kUninitializedContext);
  return CipherEncrypt(mode_, *key_, iv, in, pad_);
}
Res<ByteVector> CipherCtxBase::Decrypt(ByteView iv, ByteView in) const {
  if (!key_) return Fail<ByteVector>(CryptoErrc::kUninitializedContext);
  return CipherDecrypt(mode_, *key_, iv, in, pad_);
}

Res<void> AuthCipherCtx::SetKey(SecretKey key) {
  if (!key.IsValid()) return FailV(CryptoErrc::kEmptyContainer);
  key_.emplace(std::move(key));
  return {};
}
Res<ByteVector> AuthCipherCtx::Encrypt(ByteView nonce, ByteView aad, ByteView pt) const {
  if (!key_) return Fail<ByteVector>(CryptoErrc::kUninitializedContext);
  return AeadEncrypt(*key_, nonce, aad, pt);
}
Res<ByteVector> AuthCipherCtx::Decrypt(ByteView nonce, ByteView aad, ByteView sealed) const {
  if (!key_) return Fail<ByteVector>(CryptoErrc::kUninitializedContext);
  return AeadDecrypt(*key_, nonce, aad, sealed);
}

Res<void> SymmetricKeyWrapperCtx::SetKey(SecretKey kek) {
  if (!kek.IsValid()) return FailV(CryptoErrc::kEmptyContainer);
  kek_.emplace(std::move(kek));
  return {};
}
Res<ByteVector> SymmetricKeyWrapperCtx::WrapKey(SecretKey const &key) const {
  if (!kek_) return Fail<ByteVector>(CryptoErrc::kUninitializedContext);
  return ara::crypto::WrapKey(*kek_, key);
}
Res<SecretKey> SymmetricKeyWrapperCtx::UnwrapKey(ByteView wrapped, std::uint32_t usage, bool exportable) const {
  if (!kek_) return Fail<SecretKey>(CryptoErrc::kUninitializedContext);
  return ara::crypto::UnwrapKey(*kek_, wrapped, usage, exportable);
}

// --------------------------------------------------------------------- RSA
Res<void> EncryptorPublicCtx::SetKey(RsaPublicKey key) { key_.emplace(std::move(key)); return {}; }
Res<ByteVector> EncryptorPublicCtx::Encrypt(ByteView in) const {
  if (!key_) return Fail<ByteVector>(CryptoErrc::kUninitializedContext);
  return key_->EncryptOaep(in);
}
Res<void> DecryptorPrivateCtx::SetKey(RsaPrivateKey key) { key_.emplace(std::move(key)); return {}; }
Res<ByteVector> DecryptorPrivateCtx::Decrypt(ByteView in) const {
  if (!key_) return Fail<ByteVector>(CryptoErrc::kUninitializedContext);
  return key_->DecryptOaep(in);
}
Res<void> RsaSignerPrivateCtx::SetKey(RsaPrivateKey key) { key_.emplace(std::move(key)); return {}; }
Res<ByteVector> RsaSignerPrivateCtx::Sign(ByteView m) const {
  if (!key_) return Fail<ByteVector>(CryptoErrc::kUninitializedContext);
  return key_->SignPss(m);
}
Res<void> RsaVerifierPublicCtx::SetKey(RsaPublicKey key) { key_.emplace(std::move(key)); return {}; }
Res<bool> RsaVerifierPublicCtx::Verify(ByteView m, ByteView sig) const {
  if (!key_) return Fail<bool>(CryptoErrc::kUninitializedContext);
  return key_->VerifyPss(m, sig);
}

// ----------------------------------------------------------------- factories
Res<std::unique_ptr<KeyDerivationFunctionCtx>> CryptoProvider::CreateKeyDerivationFunctionCtx(KdfKind kind,
                                                                                              HashAlgorithm alg) const {
  return Make<KeyDerivationFunctionCtx>(kind, alg);
}
Res<std::unique_ptr<SymmetricBlockCipherCtx>> CryptoProvider::CreateSymmetricBlockCipherCtx(bool pad) const {
  return Make<SymmetricBlockCipherCtx>(pad);
}
Res<std::unique_ptr<StreamCipherCtx>> CryptoProvider::CreateStreamCipherCtx() const { return Make<StreamCipherCtx>(); }
Res<std::unique_ptr<AuthCipherCtx>> CryptoProvider::CreateAuthCipherCtx() const { return Make<AuthCipherCtx>(); }
Res<std::unique_ptr<SymmetricKeyWrapperCtx>> CryptoProvider::CreateSymmetricKeyWrapperCtx() const {
  return Make<SymmetricKeyWrapperCtx>();
}
Res<std::unique_ptr<EncryptorPublicCtx>> CryptoProvider::CreateEncryptorPublicCtx() const {
  return Make<EncryptorPublicCtx>();
}
Res<std::unique_ptr<DecryptorPrivateCtx>> CryptoProvider::CreateDecryptorPrivateCtx() const {
  return Make<DecryptorPrivateCtx>();
}
Res<std::unique_ptr<RsaSignerPrivateCtx>> CryptoProvider::CreateRsaSignerPrivateCtx() const {
  return Make<RsaSignerPrivateCtx>();
}
Res<std::unique_ptr<RsaVerifierPublicCtx>> CryptoProvider::CreateRsaVerifierPublicCtx() const {
  return Make<RsaVerifierPublicCtx>();
}
Res<void> CryptoProvider::CreateKeyEncapsulatorPublicCtx() const { return FailV(CryptoErrc::kUnsupported); }
Res<void> CryptoProvider::CreateSigEncodePrivateCtx() const { return FailV(CryptoErrc::kUnsupported); }
Res<void> CryptoProvider::CreateMsgRecoveryPublicCtx() const { return FailV(CryptoErrc::kUnsupported); }

Res<std::shared_ptr<X509Provider>> LoadX509Provider() {
  static std::shared_ptr<X509Provider> const p = std::make_shared<X509Provider>();
  return Res<std::shared_ptr<X509Provider>>::FromValue(p);
}
Res<std::shared_ptr<KeyStorageProvider>> LoadKeyStorageProvider() {
  static std::shared_ptr<KeyStorageProvider> const p =
      std::make_shared<KeyStorageProvider>(KeyStorageProvider::CreateVolatile());
  return Res<std::shared_ptr<KeyStorageProvider>>::FromValue(p);
}

} // namespace ara::crypto::cryp
