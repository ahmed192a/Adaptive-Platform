/**
 * @file crypto.h
 * @brief ara::crypto - a compact cryptography provider on top of OpenSSL.
 *
 * Offers: random generation, SHA-2 hashing, HMAC, AES-GCM AEAD, Ed25519 and
 * ECDSA-P256 signatures, and a usage-restricted SecretKey / KeyStore.
 *
 * This is NOT yet the SWS CryptoProvider/CryptoContext object model (CryptoProvider,
 * KeySlot, X509 ...); it is the primitive layer that facade will use.
 * All fallible operations return ara::core::Result; nothing throws.
 */
#ifndef ARA_CRYPTO_CRYPTO_H_
#define ARA_CRYPTO_CRYPTO_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ara/core/result.h"
#include "ara/crypto/crypto_error_domain.h"

namespace ara::crypto {

using ByteVector = std::vector<std::uint8_t>;
template <typename T> using Res = ara::core::Result<T>;

/// Non-owning view of bytes.
class ByteView {
public:
  ByteView() noexcept = default;
  ByteView(std::uint8_t const *d, std::size_t n) noexcept : data_(d), size_(n) {}
  ByteView(ByteVector const &v) noexcept : data_(v.data()), size_(v.size()) {} // NOLINT
  ByteView(std::string const &s) noexcept // NOLINT
      : data_(reinterpret_cast<std::uint8_t const *>(s.data())), size_(s.size()) {}
  std::uint8_t const *data() const noexcept { return data_; }
  std::size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return size_ == 0; }

private:
  std::uint8_t const *data_{nullptr};
  std::size_t size_{0};
};

/// Constant-time comparison.
bool ConstantTimeEquals(ByteView a, ByteView b) noexcept;
/// Overwrites memory in a way the optimizer cannot remove.
void SecureZero(void *p, std::size_t n) noexcept;

// -------------------------------------------------------------- random
Res<ByteVector> GenerateRandom(std::size_t size);

// ---------------------------------------------------------------- hash
enum class HashAlgorithm { kSha256, kSha384, kSha512 };
std::size_t DigestSize(HashAlgorithm alg) noexcept;

class HashFunction {
public:
  static Res<HashFunction> Create(HashAlgorithm alg);
  HashFunction(HashFunction &&) noexcept;
  HashFunction &operator=(HashFunction &&) noexcept;
  ~HashFunction();
  Res<void> Update(ByteView data);
  /// Returns the digest and resets the context for a new message.
  Res<ByteVector> Finish();

private:
  struct Impl;
  explicit HashFunction(std::unique_ptr<Impl> i);
  std::unique_ptr<Impl> impl_;
};
Res<ByteVector> Hash(HashAlgorithm alg, ByteView data);

// ----------------------------------------------------------- secret key
enum KeyUsage : std::uint32_t {
  kUsageNone = 0,
  kUsageEncrypt = 1U << 0,
  kUsageDecrypt = 1U << 1,
  kUsageMac = 1U << 2,        ///< HMAC generate and verify
  kUsageAll = 0x7,
};

struct KeyAccess; ///< internal accessor used by extras.cpp

/// Symmetric key material with a usage whitelist.  Zeroized on destruction.
class SecretKey {
public:
  SecretKey() = default;
  SecretKey(SecretKey const &) = delete;
  SecretKey &operator=(SecretKey const &) = delete;
  SecretKey(SecretKey &&o) noexcept;
  SecretKey &operator=(SecretKey &&o) noexcept;
  ~SecretKey();

  static Res<SecretKey> Import(ByteView material, std::uint32_t usage, bool exportable = false);
  static Res<SecretKey> Generate(std::size_t sizeBytes, std::uint32_t usage,
                                 bool exportable = false);

  std::size_t Size() const noexcept { return material_.size(); }
  std::uint32_t Usage() const noexcept { return usage_; }
  bool IsExportable() const noexcept { return exportable_; }
  bool IsValid() const noexcept { return !material_.empty(); }
  Res<ByteVector> Export() const;

private:
  friend class MessageAuthnCode;
  friend struct KeyAccess;
  friend Res<ByteVector> AeadSeal(SecretKey const &, ByteView, ByteView);
  friend Res<ByteVector> AeadOpen(SecretKey const &, ByteView, ByteView);
  friend Res<ByteVector> AeadEncrypt(SecretKey const &, ByteView, ByteView, ByteView);
  friend Res<ByteVector> AeadDecrypt(SecretKey const &, ByteView, ByteView, ByteView);
  ByteVector material_;
  std::uint32_t usage_{0};
  bool exportable_{false};
};

// ----------------------------------------------------------------- HMAC
class MessageAuthnCode {
public:
  static Res<MessageAuthnCode> Create(HashAlgorithm alg, SecretKey const &key);
  MessageAuthnCode(MessageAuthnCode &&) noexcept;
  MessageAuthnCode &operator=(MessageAuthnCode &&) noexcept;
  ~MessageAuthnCode();
  Res<void> Update(ByteView data);
  /// Returns the tag; the context is reset to the same key.
  Res<ByteVector> Finish();
  /// Convenience: computes the tag over `data` and compares in constant time.
  Res<bool> Verify(ByteView data, ByteView tag);

private:
  struct Impl;
  explicit MessageAuthnCode(std::unique_ptr<Impl> i);
  std::unique_ptr<Impl> impl_;
};

// ------------------------------------------------------------- AES-GCM
inline constexpr std::size_t kGcmNonceSize = 12;
inline constexpr std::size_t kGcmTagSize = 16;

/// Output = ciphertext || tag(16).  Nonce must be 12 bytes and never reused with a key.
Res<ByteVector> AeadEncrypt(SecretKey const &key, ByteView nonce, ByteView aad, ByteView plaintext);
/// Input = ciphertext || tag.  kAuthTagNotValid on any tampering.
Res<ByteVector> AeadDecrypt(SecretKey const &key, ByteView nonce, ByteView aad, ByteView sealed);
/// Generates a random nonce: output = nonce(12) || ciphertext || tag.
Res<ByteVector> AeadSeal(SecretKey const &key, ByteView aad, ByteView plaintext);
Res<ByteVector> AeadOpen(SecretKey const &key, ByteView aad, ByteView sealed);

// ---------------------------------------------------------- signatures
enum class SignatureAlgorithm { kEd25519, kEcdsaP256Sha256 };

class PublicKey {
public:
  PublicKey(PublicKey &&) noexcept;
  PublicKey &operator=(PublicKey &&) noexcept;
  ~PublicKey();
  static Res<PublicKey> FromDer(ByteView subjectPublicKeyInfo);
  Res<ByteVector> ToDer() const;
  SignatureAlgorithm Algorithm() const noexcept { return alg_; }
  Res<bool> Verify(ByteView message, ByteView signature) const;

private:
  friend class PrivateKey;
  struct Impl;
  PublicKey(std::unique_ptr<Impl> i, SignatureAlgorithm a);
  std::unique_ptr<Impl> impl_;
  SignatureAlgorithm alg_;
};

class PrivateKey {
public:
  PrivateKey(PrivateKey &&) noexcept;
  PrivateKey &operator=(PrivateKey &&) noexcept;
  ~PrivateKey();
  static Res<PrivateKey> Generate(SignatureAlgorithm alg);
  /// Imports an unencrypted PKCS#8 PrivateKeyInfo (Ed25519 or EC P-256 only).
  static Res<PrivateKey> FromPkcs8Der(ByteView der);
  /// Serialises as unencrypted PKCS#8.  The caller must SecureZero the result.
  Res<ByteVector> ToPkcs8Der() const;
  SignatureAlgorithm Algorithm() const noexcept { return alg_; }
  Res<ByteVector> Sign(ByteView message) const;
  Res<PublicKey> GetPublicKey() const;

private:
  struct Impl;
  PrivateKey(std::unique_ptr<Impl> i, SignatureAlgorithm a);
  std::unique_ptr<Impl> impl_;
  SignatureAlgorithm alg_;
};

// -------------------------------------------------------------- keystore
/// Named, thread-safe slots holding SecretKeys.  Keys never leave the store
/// except through a copy-free scoped call, so the caller cannot keep material.
class KeyStore {
public:
  Res<void> Store(std::string const &slot, SecretKey key);
  Res<void> Erase(std::string const &slot);
  bool Contains(std::string const &slot) const;

  /// Runs `fn(key)` while the slot is locked.
  template <typename Fn> auto With(std::string const &slot, Fn &&fn) const
      -> Res<decltype(fn(std::declval<SecretKey const &>()))>;

private:
  mutable std::mutex mutex_;
  std::map<std::string, SecretKey> slots_;
};

template <typename Fn> auto KeyStore::With(std::string const &slot, Fn &&fn) const
    -> Res<decltype(fn(std::declval<SecretKey const &>()))> {
  using R = decltype(fn(std::declval<SecretKey const &>()));
  std::lock_guard<std::mutex> lk(mutex_);
  auto it = slots_.find(slot);
  if (it == slots_.end()) return Res<R>::FromError(MakeErrorCode(CryptoErrc::kUnknownIdentifier));
  return Res<R>::FromValue(fn(it->second));
}

} // namespace ara::crypto

#endif // ARA_CRYPTO_CRYPTO_H_
