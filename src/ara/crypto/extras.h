/**
 * @file extras.h
 * @brief Additional OpenSSL-backed primitives for ara::crypto: key derivation, AES-CBC/CTR,
 *        AES key wrap, RSA (PSS / OAEP), SecretSeed and X.509 certificates.
 *
 * Like crypto.h this is the primitive layer; the SWS-shaped factories in provider.h wrap it.
 * All fallible operations return ara::core::Result; nothing throws.
 */
#ifndef ARA_CRYPTO_EXTRAS_H_
#define ARA_CRYPTO_EXTRAS_H_

#include <cstdint>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ara/crypto/crypto.h"

namespace ara::crypto {

// ------------------------------------------------------------ key derivation
/// HKDF (RFC 5869).  `length` <= 255 * digest size.
Res<ByteVector> Hkdf(HashAlgorithm alg, ByteView ikm, ByteView salt, ByteView info, std::size_t length);
/// PBKDF2-HMAC (RFC 8018).  By default OpenSSL's lower bounds are enforced (salt >= 16 bytes,
/// iterations >= 1000, output >= 14 bytes); `allowWeakParameters` disables them (known-answer tests only).
Res<ByteVector> Pbkdf2(HashAlgorithm alg, ByteView password, ByteView salt, std::uint32_t iterations,
                       std::size_t length, bool allowWeakParameters = false);
/// Convenience: HKDF output wrapped as a SecretKey with the given usage.
Res<SecretKey> HkdfDeriveKey(HashAlgorithm alg, ByteView ikm, ByteView salt, ByteView info,
                             std::size_t keySize, std::uint32_t usage, bool exportable = false);

// ------------------------------------------------------- AES-CBC / AES-CTR
enum class CipherMode { kCbc, kCtr };
inline constexpr std::size_t kAesBlockSize = 16;

/// Key must be 16/24/32 bytes and carry kUsageEncrypt; IV must be 16 bytes.
/// CBC uses PKCS#7 padding when `pkcs7Padding` is true (otherwise input must be block-aligned);
/// CTR is a stream mode (no padding, any length).
Res<ByteVector> CipherEncrypt(CipherMode mode, SecretKey const &key, ByteView iv, ByteView plaintext,
                              bool pkcs7Padding = true);
/// Requires kUsageDecrypt.  Bad padding -> kInvalidInputSize / kAuthTagNotValid (never an oracle detail).
Res<ByteVector> CipherDecrypt(CipherMode mode, SecretKey const &key, ByteView iv, ByteView ciphertext,
                              bool pkcs7Padding = true);

// ------------------------------------------------------------ AES key wrap
/// RFC 3394.  `kek` needs kUsageEncrypt (wrap) / kUsageDecrypt (unwrap); the wrapped key must be
/// exportable and a multiple of 8 bytes (>= 16).
Res<ByteVector> WrapKey(SecretKey const &kek, SecretKey const &keyToWrap);
Res<SecretKey> UnwrapKey(SecretKey const &kek, ByteView wrapped, std::uint32_t usage, bool exportable = false);

// ---------------------------------------------------------------- SecretSeed
/// Raw secret bytes that can be combined and stepped before becoming a key (SWS SecretSeed shape).
class SecretSeed {
public:
  static Res<SecretSeed> Generate(std::size_t sizeBytes);
  static Res<SecretSeed> Import(ByteView material);
  SecretSeed(SecretSeed &&) noexcept = default;
  SecretSeed &operator=(SecretSeed &&) noexcept = default;
  SecretSeed(SecretSeed const &) = delete;
  SecretSeed &operator=(SecretSeed const &) = delete;
  ~SecretSeed();

  std::size_t Size() const noexcept { return bytes_.size(); }
  /// Byte-wise XOR; sizes must match.
  Res<void> XorWith(SecretSeed const &other);
  /// Treats the seed as a big-endian counter and adds `steps` (wraps around).
  void Jump(std::int64_t steps) noexcept;
  void Next() noexcept { Jump(1); }
  /// Copies the seed into a key (size must be a valid key size for the use).
  Res<SecretKey> ToKey(std::uint32_t usage, bool exportable = false) const;
  /// Derives a key of `keySize` bytes via HKDF-SHA256 using `info`.
  Res<SecretKey> DeriveKey(ByteView info, std::size_t keySize, std::uint32_t usage) const;

private:
  explicit SecretSeed(ByteVector b) noexcept : bytes_(std::move(b)) {}
  ByteVector bytes_;
};

// ---------------------------------------------------------------------- RSA
class RsaPublicKey {
public:
  RsaPublicKey(RsaPublicKey &&) noexcept;
  RsaPublicKey &operator=(RsaPublicKey &&) noexcept;
  ~RsaPublicKey();
  static Res<RsaPublicKey> FromDer(ByteView subjectPublicKeyInfo);
  Res<ByteVector> ToDer() const;
  std::size_t ModulusBits() const noexcept;
  /// RSASSA-PSS with SHA-256, salt length = digest length.
  Res<bool> VerifyPss(ByteView message, ByteView signature) const;
  /// RSAES-OAEP with SHA-256.
  Res<ByteVector> EncryptOaep(ByteView plaintext) const;

private:
  friend class RsaPrivateKey;
  struct Impl;
  explicit RsaPublicKey(std::unique_ptr<Impl> i);
  std::unique_ptr<Impl> impl_;
};

class RsaPrivateKey {
public:
  RsaPrivateKey(RsaPrivateKey &&) noexcept;
  RsaPrivateKey &operator=(RsaPrivateKey &&) noexcept;
  ~RsaPrivateKey();
  /// `bits` must be 2048, 3072 or 4096.
  static Res<RsaPrivateKey> Generate(unsigned bits = 2048);
  static Res<RsaPrivateKey> FromPkcs8Der(ByteView der);
  Res<ByteVector> ToPkcs8Der() const;
  Res<RsaPublicKey> GetPublicKey() const;
  Res<ByteVector> SignPss(ByteView message) const;
  Res<ByteVector> DecryptOaep(ByteView ciphertext) const;

private:
  struct Impl;
  explicit RsaPrivateKey(std::unique_ptr<Impl> i);
  std::unique_ptr<Impl> impl_;
};

// --------------------------------------------------------------------- X.509
/// Everything needed to issue a certificate.  `subjectPublicKeyDer` is an SPKI.
struct CertificateRequest {
  std::string commonName;
  ByteVector subjectPublicKeyDer;
  std::uint64_t serial{1};
  std::time_t notBefore{0};   ///< 0 = now
  std::time_t notAfter{0};    ///< 0 = notBefore + 365 days
  bool isCa{false};
};

class X509Certificate {
public:
  struct Impl; ///< opaque
  explicit X509Certificate(std::shared_ptr<Impl> i) noexcept;
  X509Certificate(X509Certificate &&) noexcept;
  X509Certificate &operator=(X509Certificate &&) noexcept;
  X509Certificate(X509Certificate const &);
  X509Certificate &operator=(X509Certificate const &);
  ~X509Certificate();

  static Res<X509Certificate> FromDer(ByteView der);
  static Res<X509Certificate> FromPem(std::string const &pem);
  Res<ByteVector> ToDer() const;
  Res<std::string> ToPem() const;

  /// RFC 2253 style one-line names, e.g. "CN=Root CA".
  std::string SubjectName() const;
  std::string IssuerName() const;
  std::string SerialNumberHex() const;
  std::time_t NotBefore() const noexcept;
  std::time_t NotAfter() const noexcept;
  bool IsCa() const noexcept;
  bool IsSelfIssued() const noexcept;
  /// SubjectPublicKeyInfo DER.
  Res<ByteVector> GetPublicKeyDer() const;
  /// True when this certificate's signature verifies under `issuerPublicKeyDer` (SPKI).
  Res<bool> IsSignedBy(ByteView issuerPublicKeyDer) const;
  /// SHA-256 fingerprint of the DER encoding.
  Res<ByteVector> Fingerprint() const;

  std::shared_ptr<Impl> const &RawImpl() const noexcept { return impl_; }

private:
  std::shared_ptr<Impl> impl_;
};

/// Issues a certificate signed by `issuerKey`.  `issuer == nullptr` produces a self-signed certificate
/// (the request's subject key must then belong to `issuerKey`).
Res<X509Certificate> IssueCertificate(CertificateRequest const &req, X509Certificate const *issuer,
                                      PrivateKey const &issuerKey);

/// Trust-anchor store and chain validation (SWS X509Provider shape).
class X509Provider {
public:
  /// Anchors must be CA certificates.
  Res<void> AddTrustAnchor(X509Certificate const &anchor);
  void ClearTrustAnchors();
  std::size_t TrustAnchorCount() const;
  /// Validates `leaf` through `intermediates` to a trust anchor at time `at` (0 = now): signatures, validity
  /// period, basicConstraints/CA flags.  On failure the error is kAuthTagNotValid whose support data is the
  /// OpenSSL X509_V_ERR_* code.
  Res<void> VerifyChain(X509Certificate const &leaf, std::vector<X509Certificate> const &intermediates = {},
                        std::time_t at = 0) const;

private:
  mutable std::mutex mutex_;
  std::vector<X509Certificate> anchors_;
};

} // namespace ara::crypto

#endif // ARA_CRYPTO_EXTRAS_H_
