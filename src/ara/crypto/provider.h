/**
 * @file provider.h
 * @brief SWS-shaped facade (ara::crypto::cryp): CryptoProvider and the Ctx classes.
 *
 * PRAGMATIC SUBSET.  The factory names (LoadCryptoProvider, CreateHashFunctionCtx,
 * CreateMessageAuthnCodeCtx, CreateSignerPrivateCtx, CreateVerifierPublicCtx, CreateRandomGeneratorCtx)
 * follow SWS Cryptography; method signatures are simplified (no CryptoPrimitiveId strings, no
 * ReadOnlyMemRegion, no CryptoObject/KeyMaterial hierarchy) and are listed as Partial in
 * doc/spec-conformance/crypto.md.  Contexts are thin wrappers over the OpenSSL-backed primitives
 * in crypto.h.
 */
#ifndef ARA_CRYPTO_PROVIDER_H_
#define ARA_CRYPTO_PROVIDER_H_

#include <memory>
#include <optional>

#include "ara/crypto/crypto.h"
#include "ara/crypto/extras.h"
#include "ara/crypto/key_slot.h"
#include "ara/crypto/crypto_error_domain.h"

namespace ara::crypto::cryp {

/// Hash context: Start -> Update* -> Finish; restartable.
class HashFunctionCtx {
public:
  explicit HashFunctionCtx(HashAlgorithm alg) noexcept : alg_(alg) {}
  HashAlgorithm Algorithm() const noexcept { return alg_; }
  std::size_t GetDigestSize() const noexcept { return DigestSize(alg_); }
  Res<void> Start();
  Res<void> Update(ByteView in);
  Res<ByteVector> Finish();
  bool IsStarted() const noexcept { return h_.has_value(); }

private:
  HashAlgorithm alg_;
  std::optional<HashFunction> h_;
};

/// HMAC context.  SetKey requires KeyUsage kUsageMac.  After Finish the key must be set again
/// (deviation: SecretKey is move-only so the context cannot re-key itself).
class MessageAuthnCodeCtx {
public:
  explicit MessageAuthnCodeCtx(HashAlgorithm alg) noexcept : alg_(alg) {}
  Res<void> SetKey(SecretKey const &key);
  Res<void> Start();
  Res<void> Update(ByteView in);
  Res<ByteVector> Finish();
  /// Compares in constant time.
  Res<bool> Check(ByteView expectedTag);
  bool IsKeySet() const noexcept { return m_.has_value(); }

private:
  HashAlgorithm alg_;
  std::optional<MessageAuthnCode> m_;
  bool started_{false};
};

class SignerPrivateCtx {
public:
  explicit SignerPrivateCtx(SignatureAlgorithm alg) noexcept : alg_(alg) {}
  Res<void> SetKey(PrivateKey key);
  Res<ByteVector> Sign(ByteView message) const;
  bool IsKeySet() const noexcept { return key_.has_value(); }

private:
  SignatureAlgorithm alg_;
  std::optional<PrivateKey> key_;
};

class VerifierPublicCtx {
public:
  explicit VerifierPublicCtx(SignatureAlgorithm alg) noexcept : alg_(alg) {}
  Res<void> SetKey(PublicKey key);
  Res<bool> Verify(ByteView message, ByteView signature) const;
  bool IsKeySet() const noexcept { return key_.has_value(); }

private:
  SignatureAlgorithm alg_;
  std::optional<PublicKey> key_;
};

class RandomGeneratorCtx {
public:
  Res<ByteVector> Generate(std::size_t count) const;
};

/// Key derivation (HKDF or PBKDF2).  Configure() first, then DeriveKey()/DeriveBytes().
enum class KdfKind { kHkdf, kPbkdf2 };
class KeyDerivationFunctionCtx {
public:
  KeyDerivationFunctionCtx(KdfKind kind, HashAlgorithm alg) noexcept : kind_(kind), alg_(alg) {}
  /// HKDF: salt + info.  PBKDF2: salt + iterations (info ignored).
  Res<void> Configure(ByteView salt, ByteView info, std::uint32_t iterations = 0);
  Res<ByteVector> DeriveBytes(ByteView secret, std::size_t length) const;
  Res<SecretKey> DeriveKey(ByteView secret, std::size_t keySize, std::uint32_t usage,
                           bool exportable = false) const;
  bool IsConfigured() const noexcept { return configured_; }
  /// PBKDF2 only: disable OpenSSL's minimum salt/iteration/length checks (known-answer tests).
  void AllowWeakParameters(bool allow) noexcept { weak_ = allow; }

private:
  bool weak_{false};
  KdfKind kind_;
  HashAlgorithm alg_;
  ByteVector salt_, info_;
  std::uint32_t iterations_{0};
  bool configured_{false};
};

/// AES-CBC (SymmetricBlockCipherCtx) or AES-CTR (StreamCipherCtx).  SetKey takes ownership.
class CipherCtxBase {
public:
  Res<void> SetKey(SecretKey key);
  bool IsKeySet() const noexcept { return key_.has_value(); }
  Res<ByteVector> Encrypt(ByteView iv, ByteView in) const;
  Res<ByteVector> Decrypt(ByteView iv, ByteView in) const;

protected:
  CipherCtxBase(CipherMode m, bool pad) noexcept : mode_(m), pad_(pad) {}

private:
  CipherMode mode_;
  bool pad_;
  std::optional<SecretKey> key_;
};
class SymmetricBlockCipherCtx : public CipherCtxBase {
public:
  explicit SymmetricBlockCipherCtx(bool pkcs7Padding = true) noexcept : CipherCtxBase(CipherMode::kCbc, pkcs7Padding) {}
};
class StreamCipherCtx : public CipherCtxBase {
public:
  StreamCipherCtx() noexcept : CipherCtxBase(CipherMode::kCtr, false) {}
};

/// AES-GCM context.
class AuthCipherCtx {
public:
  Res<void> SetKey(SecretKey key);
  bool IsKeySet() const noexcept { return key_.has_value(); }
  Res<ByteVector> Encrypt(ByteView nonce, ByteView aad, ByteView plaintext) const;
  Res<ByteVector> Decrypt(ByteView nonce, ByteView aad, ByteView sealed) const;

private:
  std::optional<SecretKey> key_;
};

/// AES key wrap (RFC 3394).
class SymmetricKeyWrapperCtx {
public:
  Res<void> SetKey(SecretKey kek);
  bool IsKeySet() const noexcept { return kek_.has_value(); }
  Res<ByteVector> WrapKey(SecretKey const &key) const;
  Res<SecretKey> UnwrapKey(ByteView wrapped, std::uint32_t usage, bool exportable = false) const;

private:
  std::optional<SecretKey> kek_;
};

/// RSA contexts (OAEP encryption, PSS signatures; SHA-256).
class EncryptorPublicCtx {
public:
  Res<void> SetKey(RsaPublicKey key);
  Res<ByteVector> Encrypt(ByteView in) const;
private:
  std::optional<RsaPublicKey> key_;
};
class DecryptorPrivateCtx {
public:
  Res<void> SetKey(RsaPrivateKey key);
  Res<ByteVector> Decrypt(ByteView in) const;
private:
  std::optional<RsaPrivateKey> key_;
};
class RsaSignerPrivateCtx {
public:
  Res<void> SetKey(RsaPrivateKey key);
  Res<ByteVector> Sign(ByteView message) const;
private:
  std::optional<RsaPrivateKey> key_;
};
class RsaVerifierPublicCtx {
public:
  Res<void> SetKey(RsaPublicKey key);
  Res<bool> Verify(ByteView message, ByteView signature) const;
private:
  std::optional<RsaPublicKey> key_;
};

class CryptoProvider {
public:
  /// Provider identifier (implementation-defined name).
  char const *GetProviderName() const noexcept { return "ap-openssl"; }

  Res<std::unique_ptr<HashFunctionCtx>> CreateHashFunctionCtx(HashAlgorithm alg) const;
  Res<std::unique_ptr<MessageAuthnCodeCtx>> CreateMessageAuthnCodeCtx(HashAlgorithm alg) const;
  Res<std::unique_ptr<SignerPrivateCtx>> CreateSignerPrivateCtx(SignatureAlgorithm alg) const;
  Res<std::unique_ptr<VerifierPublicCtx>> CreateVerifierPublicCtx(SignatureAlgorithm alg) const;
  Res<std::unique_ptr<RandomGeneratorCtx>> CreateRandomGeneratorCtx() const;

  Res<std::unique_ptr<KeyDerivationFunctionCtx>> CreateKeyDerivationFunctionCtx(KdfKind kind, HashAlgorithm alg) const;
  Res<std::unique_ptr<SymmetricBlockCipherCtx>> CreateSymmetricBlockCipherCtx(bool pkcs7Padding = true) const;
  Res<std::unique_ptr<StreamCipherCtx>> CreateStreamCipherCtx() const;
  Res<std::unique_ptr<AuthCipherCtx>> CreateAuthCipherCtx() const;
  Res<std::unique_ptr<SymmetricKeyWrapperCtx>> CreateSymmetricKeyWrapperCtx() const;
  Res<std::unique_ptr<EncryptorPublicCtx>> CreateEncryptorPublicCtx() const;
  Res<std::unique_ptr<DecryptorPrivateCtx>> CreateDecryptorPrivateCtx() const;
  Res<std::unique_ptr<RsaSignerPrivateCtx>> CreateRsaSignerPrivateCtx() const;
  Res<std::unique_ptr<RsaVerifierPublicCtx>> CreateRsaVerifierPublicCtx() const;
  /// Not implemented (no KEM / message-recovery / signature-encoding support): always kUnsupported.
  Res<void> CreateKeyEncapsulatorPublicCtx() const;
  Res<void> CreateSigEncodePrivateCtx() const;
  Res<void> CreateMsgRecoveryPublicCtx() const;
};

/// SWS factory function.  Always succeeds for the built-in provider.
Res<std::shared_ptr<CryptoProvider>> LoadCryptoProvider();
/// SWS factory: the shared X.509 provider (trust anchors + chain validation).
Res<std::shared_ptr<X509Provider>> LoadX509Provider();
/// SWS factory: a shared volatile key storage provider (persistent slots need KeyStorageProvider::Create).
Res<std::shared_ptr<KeyStorageProvider>> LoadKeyStorageProvider();

} // namespace ara::crypto::cryp

#endif // ARA_CRYPTO_PROVIDER_H_
