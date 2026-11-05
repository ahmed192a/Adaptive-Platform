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

class CryptoProvider {
public:
  /// Provider identifier (implementation-defined name).
  char const *GetProviderName() const noexcept { return "ap-openssl"; }

  Res<std::unique_ptr<HashFunctionCtx>> CreateHashFunctionCtx(HashAlgorithm alg) const;
  Res<std::unique_ptr<MessageAuthnCodeCtx>> CreateMessageAuthnCodeCtx(HashAlgorithm alg) const;
  Res<std::unique_ptr<SignerPrivateCtx>> CreateSignerPrivateCtx(SignatureAlgorithm alg) const;
  Res<std::unique_ptr<VerifierPublicCtx>> CreateVerifierPublicCtx(SignatureAlgorithm alg) const;
  Res<std::unique_ptr<RandomGeneratorCtx>> CreateRandomGeneratorCtx() const;
};

/// SWS factory function.  Always succeeds for the built-in provider.
Res<std::shared_ptr<CryptoProvider>> LoadCryptoProvider();

} // namespace ara::crypto::cryp

#endif // ARA_CRYPTO_PROVIDER_H_
