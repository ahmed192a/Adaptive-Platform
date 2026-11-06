#include "ara/crypto/extras.h"

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <limits>

namespace ara::crypto {

/// Grants this translation unit access to SecretKey internals (declared a friend in crypto.h).
struct KeyAccess {
  static ByteVector const &Bytes(SecretKey const &k) noexcept { return k.material_; }
};

namespace {

template <typename T> Res<T> Fail(CryptoErrc e, ara::core::ErrorDomain::SupportDataType data = 0) {
  return Res<T>::FromError(MakeErrorCode(e, data));
}
Res<void> FailV(CryptoErrc e) { return Res<void>::FromError(MakeErrorCode(e)); }

bool FitsInt(std::size_t n) { return n <= static_cast<std::size_t>(std::numeric_limits<int>::max()); }

struct PkeyDel { void operator()(EVP_PKEY *k) const { EVP_PKEY_free(k); } };
struct PkeyCtxDel { void operator()(EVP_PKEY_CTX *c) const { EVP_PKEY_CTX_free(c); } };
struct CipherCtxDel { void operator()(EVP_CIPHER_CTX *c) const { EVP_CIPHER_CTX_free(c); } };
struct KdfDel { void operator()(EVP_KDF *k) const { EVP_KDF_free(k); } };
struct KdfCtxDel { void operator()(EVP_KDF_CTX *k) const { EVP_KDF_CTX_free(k); } };
struct X509Del { void operator()(X509 *x) const { X509_free(x); } };
struct BioDel { void operator()(BIO *b) const { BIO_free_all(b); } };
struct StoreDel { void operator()(X509_STORE *s) const { X509_STORE_free(s); } };
struct StoreCtxDel { void operator()(X509_STORE_CTX *s) const { X509_STORE_CTX_free(s); } };
struct BnDel { void operator()(BIGNUM *b) const { BN_free(b); } };
using Pkey = std::unique_ptr<EVP_PKEY, PkeyDel>;
using PkeyCtx = std::unique_ptr<EVP_PKEY_CTX, PkeyCtxDel>;
using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, CipherCtxDel>;
using Bio = std::unique_ptr<BIO, BioDel>;
using X509Ptr = std::unique_ptr<X509, X509Del>;

char const *MdName(HashAlgorithm a) {
  switch (a) {
  case HashAlgorithm::kSha256: return "SHA256";
  case HashAlgorithm::kSha384: return "SHA384";
  case HashAlgorithm::kSha512: return "SHA512";
  }
  return "SHA256";
}

EVP_CIPHER const *AesCipher(CipherMode m, std::size_t keyBytes) {
  switch (keyBytes) {
  case 16: return m == CipherMode::kCbc ? EVP_aes_128_cbc() : EVP_aes_128_ctr();
  case 24: return m == CipherMode::kCbc ? EVP_aes_192_cbc() : EVP_aes_192_ctr();
  case 32: return m == CipherMode::kCbc ? EVP_aes_256_cbc() : EVP_aes_256_ctr();
  default: return nullptr;
  }
}

} // namespace

// ------------------------------------------------------------ key derivation
namespace {
Res<ByteVector> RunKdf(char const *name, OSSL_PARAM *params, std::size_t length) {
  std::unique_ptr<EVP_KDF, KdfDel> kdf(EVP_KDF_fetch(nullptr, name, nullptr));
  if (!kdf) return Fail<ByteVector>(CryptoErrc::kUnsupported);
  std::unique_ptr<EVP_KDF_CTX, KdfCtxDel> ctx(EVP_KDF_CTX_new(kdf.get()));
  if (!ctx) return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  ByteVector out(length);
  if (EVP_KDF_derive(ctx.get(), out.data(), out.size(), params) != 1) {
    SecureZero(out.data(), out.size());
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  }
  return out;
}
} // namespace

Res<ByteVector> Hkdf(HashAlgorithm alg, ByteView ikm, ByteView salt, ByteView info, std::size_t length) {
  if (ikm.empty()) return Fail<ByteVector>(CryptoErrc::kEmptyContainer);
  if (length == 0 || length > 255 * DigestSize(alg)) return Fail<ByteVector>(CryptoErrc::kInvalidArgument);
  // OpenSSL rejects a NULL data pointer even for a zero-length octet string: point empties at a dummy byte.
  static std::uint8_t dummy = 0;
  auto ptr = [](ByteView v) { return const_cast<std::uint8_t *>(v.empty() ? &dummy : v.data()); };
  OSSL_PARAM params[] = {
      OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, const_cast<char *>(MdName(alg)), 0),
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, ptr(ikm), ikm.size()),
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, ptr(salt), salt.size()),
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, ptr(info), info.size()),
      OSSL_PARAM_construct_end()};
  return RunKdf("HKDF", params, length);
}

Res<ByteVector> Pbkdf2(HashAlgorithm alg, ByteView password, ByteView salt, std::uint32_t iterations,
                       std::size_t length, bool allowWeakParameters) {
  if (password.empty() || salt.empty()) return Fail<ByteVector>(CryptoErrc::kEmptyContainer);
  if (iterations == 0 || length == 0) return Fail<ByteVector>(CryptoErrc::kInvalidArgument);
  unsigned int iter = iterations;
  int disableLowerBounds = allowWeakParameters ? 1 : 0;
  OSSL_PARAM params[] = {
      OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, const_cast<char *>(MdName(alg)), 0),
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD, const_cast<std::uint8_t *>(password.data()),
                                        password.size()),
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, const_cast<std::uint8_t *>(salt.data()), salt.size()),
      OSSL_PARAM_construct_uint(OSSL_KDF_PARAM_ITER, &iter),
      OSSL_PARAM_construct_int(OSSL_KDF_PARAM_PKCS5, &disableLowerBounds),
      OSSL_PARAM_construct_end()};
  return RunKdf("PBKDF2", params, length);
}

Res<SecretKey> HkdfDeriveKey(HashAlgorithm alg, ByteView ikm, ByteView salt, ByteView info, std::size_t keySize,
                             std::uint32_t usage, bool exportable) {
  auto okm = Hkdf(alg, ikm, salt, info, keySize);
  if (!okm.HasValue()) return Res<SecretKey>::FromError(okm.Error());
  auto key = SecretKey::Import(okm.Value(), usage, exportable);
  SecureZero(okm.Value().data(), okm.Value().size());
  return key;
}

// ------------------------------------------------------- AES-CBC / AES-CTR
namespace {
Res<ByteVector> CipherRun(bool encrypt, CipherMode mode, SecretKey const &key, ByteView iv, ByteView in,
                          bool padding) {
  std::uint32_t const need = encrypt ? kUsageEncrypt : kUsageDecrypt;
  if ((key.Usage() & need) == 0) return Fail<ByteVector>(CryptoErrc::kUsageViolation);
  auto const &km = KeyAccess::Bytes(key);
  EVP_CIPHER const *c = AesCipher(mode, km.size());
  if (c == nullptr) return Fail<ByteVector>(CryptoErrc::kInvalidKeyLength);
  if (iv.size() != kAesBlockSize) return Fail<ByteVector>(CryptoErrc::kInvalidInputSize);
  if (!FitsInt(in.size())) return Fail<ByteVector>(CryptoErrc::kInvalidArgument);
  bool const pad = mode == CipherMode::kCbc && padding;
  if (mode == CipherMode::kCbc && !pad && in.size() % kAesBlockSize != 0)
    return Fail<ByteVector>(CryptoErrc::kInvalidInputSize);
  if (mode == CipherMode::kCbc && !encrypt && (in.empty() || in.size() % kAesBlockSize != 0))
    return Fail<ByteVector>(CryptoErrc::kInvalidInputSize);

  CipherCtx ctx(EVP_CIPHER_CTX_new());
  if (!ctx) return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  if (EVP_CipherInit_ex(ctx.get(), c, nullptr, km.data(), iv.data(), encrypt ? 1 : 0) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  EVP_CIPHER_CTX_set_padding(ctx.get(), pad ? 1 : 0);
  ByteVector out(in.size() + kAesBlockSize);
  int n1 = 0;
  int n2 = 0;
  if (in.size() > 0 &&
      EVP_CipherUpdate(ctx.get(), out.data(), &n1, in.data(), static_cast<int>(in.size())) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  if (EVP_CipherFinal_ex(ctx.get(), out.data() + n1, &n2) != 1) {
    SecureZero(out.data(), out.size());
    // padding failure on decrypt: do not expose which check failed
    return Fail<ByteVector>(encrypt ? CryptoErrc::kRuntimeFault : CryptoErrc::kAuthTagNotValid);
  }
  out.resize(static_cast<std::size_t>(n1 + n2));
  return out;
}
} // namespace

Res<ByteVector> CipherEncrypt(CipherMode mode, SecretKey const &key, ByteView iv, ByteView plaintext,
                              bool pkcs7Padding) {
  return CipherRun(true, mode, key, iv, plaintext, pkcs7Padding);
}
Res<ByteVector> CipherDecrypt(CipherMode mode, SecretKey const &key, ByteView iv, ByteView ciphertext,
                              bool pkcs7Padding) {
  return CipherRun(false, mode, key, iv, ciphertext, pkcs7Padding);
}

// ------------------------------------------------------------ AES key wrap
namespace {
EVP_CIPHER const *WrapCipher(std::size_t keyBytes) {
  switch (keyBytes) {
  case 16: return EVP_aes_128_wrap();
  case 24: return EVP_aes_192_wrap();
  case 32: return EVP_aes_256_wrap();
  default: return nullptr;
  }
}
Res<ByteVector> WrapRun(bool wrap, SecretKey const &kek, ByteView in) {
  auto const &km = KeyAccess::Bytes(kek);
  EVP_CIPHER const *c = WrapCipher(km.size());
  if (c == nullptr) return Fail<ByteVector>(CryptoErrc::kInvalidKeyLength);
  if (in.size() < 16 || in.size() % 8 != 0 || !FitsInt(in.size())) return Fail<ByteVector>(CryptoErrc::kInvalidInputSize);
  CipherCtx ctx(EVP_CIPHER_CTX_new());
  if (!ctx) return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  EVP_CIPHER_CTX_set_flags(ctx.get(), EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);
  if (EVP_CipherInit_ex(ctx.get(), c, nullptr, km.data(), nullptr, wrap ? 1 : 0) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  ByteVector out(in.size() + 16);
  int n1 = 0;
  int n2 = 0;
  if (EVP_CipherUpdate(ctx.get(), out.data(), &n1, in.data(), static_cast<int>(in.size())) != 1 ||
      EVP_CipherFinal_ex(ctx.get(), out.data() + n1, &n2) != 1) {
    SecureZero(out.data(), out.size());
    return Fail<ByteVector>(wrap ? CryptoErrc::kRuntimeFault : CryptoErrc::kAuthTagNotValid);
  }
  out.resize(static_cast<std::size_t>(n1 + n2));
  return out;
}
} // namespace

Res<ByteVector> WrapKey(SecretKey const &kek, SecretKey const &keyToWrap) {
  if ((kek.Usage() & kUsageEncrypt) == 0) return Fail<ByteVector>(CryptoErrc::kUsageViolation);
  if (!keyToWrap.IsExportable()) return Fail<ByteVector>(CryptoErrc::kKeyNotExportable);
  return WrapRun(true, kek, KeyAccess::Bytes(keyToWrap));
}

Res<SecretKey> UnwrapKey(SecretKey const &kek, ByteView wrapped, std::uint32_t usage, bool exportable) {
  if ((kek.Usage() & kUsageDecrypt) == 0) return Fail<SecretKey>(CryptoErrc::kUsageViolation);
  auto raw = WrapRun(false, kek, wrapped);
  if (!raw.HasValue()) return Res<SecretKey>::FromError(raw.Error());
  auto key = SecretKey::Import(raw.Value(), usage, exportable);
  SecureZero(raw.Value().data(), raw.Value().size());
  return key;
}

// ---------------------------------------------------------------- SecretSeed
SecretSeed::~SecretSeed() { SecureZero(bytes_.data(), bytes_.size()); }

Res<SecretSeed> SecretSeed::Generate(std::size_t sizeBytes) {
  if (sizeBytes == 0 || sizeBytes > 1024) return Fail<SecretSeed>(CryptoErrc::kInvalidArgument);
  auto r = GenerateRandom(sizeBytes);
  if (!r.HasValue()) return Res<SecretSeed>::FromError(r.Error());
  return SecretSeed(std::move(r).Value());
}

Res<SecretSeed> SecretSeed::Import(ByteView material) {
  if (material.empty()) return Fail<SecretSeed>(CryptoErrc::kEmptyContainer);
  return SecretSeed(ByteVector(material.data(), material.data() + material.size()));
}

Res<void> SecretSeed::XorWith(SecretSeed const &other) {
  if (other.bytes_.size() != bytes_.size()) return FailV(CryptoErrc::kInvalidInputSize);
  for (std::size_t i = 0; i < bytes_.size(); ++i)
    bytes_[i] = static_cast<std::uint8_t>(bytes_[i] ^ other.bytes_[i]);
  return Res<void>::FromValue();
}

void SecretSeed::Jump(std::int64_t steps) noexcept {
  bool const negative = steps < 0;
  std::uint64_t mag = negative ? static_cast<std::uint64_t>(-(steps + 1)) + 1U : static_cast<std::uint64_t>(steps);
  // add or subtract `mag` from the big-endian value, byte by byte from the least significant end
  unsigned carry = 0;
  for (std::size_t i = 0; i < bytes_.size(); ++i) {
    std::uint8_t &b = bytes_[bytes_.size() - 1 - i];
    unsigned const addend = static_cast<unsigned>(mag & 0xFFU);
    mag >>= 8;
    if (!negative) {
      unsigned const sum = static_cast<unsigned>(b) + addend + carry;
      b = static_cast<std::uint8_t>(sum & 0xFFU);
      carry = sum >> 8;
    } else {
      int const diff = static_cast<int>(b) - static_cast<int>(addend) - static_cast<int>(carry);
      b = static_cast<std::uint8_t>(diff & 0xFF);
      carry = diff < 0 ? 1U : 0U;
    }
    if (mag == 0 && carry == 0) break;
  }
}

Res<SecretKey> SecretSeed::ToKey(std::uint32_t usage, bool exportable) const {
  return SecretKey::Import(bytes_, usage, exportable);
}

Res<SecretKey> SecretSeed::DeriveKey(ByteView info, std::size_t keySize, std::uint32_t usage) const {
  return HkdfDeriveKey(HashAlgorithm::kSha256, bytes_, ByteView{}, info, keySize, usage);
}

// ---------------------------------------------------------------------- RSA
struct RsaPublicKey::Impl { Pkey key; };
struct RsaPrivateKey::Impl { Pkey key; };

RsaPublicKey::RsaPublicKey(std::unique_ptr<Impl> i) : impl_(std::move(i)) {}
RsaPublicKey::RsaPublicKey(RsaPublicKey &&) noexcept = default;
RsaPublicKey &RsaPublicKey::operator=(RsaPublicKey &&) noexcept = default;
RsaPublicKey::~RsaPublicKey() = default;
RsaPrivateKey::RsaPrivateKey(std::unique_ptr<Impl> i) : impl_(std::move(i)) {}
RsaPrivateKey::RsaPrivateKey(RsaPrivateKey &&) noexcept = default;
RsaPrivateKey &RsaPrivateKey::operator=(RsaPrivateKey &&) noexcept = default;
RsaPrivateKey::~RsaPrivateKey() = default;

namespace {
bool IsRsa(EVP_PKEY const *k) { return k != nullptr && EVP_PKEY_base_id(k) == EVP_PKEY_RSA; }

Res<ByteVector> RsaSign(EVP_PKEY *key, ByteView msg) {
  PkeyCtx ctx(EVP_PKEY_CTX_new(key, nullptr));
  if (!ctx) return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  // sign over the SHA-256 digest with PSS padding
  auto d = Hash(HashAlgorithm::kSha256, msg);
  if (!d.HasValue()) return Res<ByteVector>::FromError(d.Error());
  if (EVP_PKEY_sign_init(ctx.get()) != 1 || EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_PSS_PADDING) != 1 ||
      EVP_PKEY_CTX_set_rsa_pss_saltlen(ctx.get(), RSA_PSS_SALTLEN_DIGEST) != 1 ||
      EVP_PKEY_CTX_set_signature_md(ctx.get(), EVP_sha256()) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  std::size_t len = 0;
  if (EVP_PKEY_sign(ctx.get(), nullptr, &len, d.Value().data(), d.Value().size()) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  ByteVector sig(len);
  if (EVP_PKEY_sign(ctx.get(), sig.data(), &len, d.Value().data(), d.Value().size()) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  sig.resize(len);
  return sig;
}

Res<ByteVector> RsaCrypt(bool encrypt, EVP_PKEY *key, ByteView in) {
  PkeyCtx ctx(EVP_PKEY_CTX_new(key, nullptr));
  if (!ctx) return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  int const init = encrypt ? EVP_PKEY_encrypt_init(ctx.get()) : EVP_PKEY_decrypt_init(ctx.get());
  if (init != 1 || EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_OAEP_PADDING) != 1 ||
      EVP_PKEY_CTX_set_rsa_oaep_md(ctx.get(), EVP_sha256()) != 1 ||
      EVP_PKEY_CTX_set_rsa_mgf1_md(ctx.get(), EVP_sha256()) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  std::size_t len = 0;
  auto run = [&](unsigned char *out) {
    return encrypt ? EVP_PKEY_encrypt(ctx.get(), out, &len, in.data(), in.size())
                   : EVP_PKEY_decrypt(ctx.get(), out, &len, in.data(), in.size());
  };
  if (run(nullptr) != 1) return Fail<ByteVector>(encrypt ? CryptoErrc::kInvalidInputSize : CryptoErrc::kAuthTagNotValid);
  ByteVector out(len);
  if (run(out.data()) != 1) {
    ERR_clear_error();
    return Fail<ByteVector>(encrypt ? CryptoErrc::kInvalidInputSize : CryptoErrc::kAuthTagNotValid);
  }
  out.resize(len);
  return out;
}
} // namespace

Res<RsaPrivateKey> RsaPrivateKey::Generate(unsigned bits) {
  if (bits != 2048 && bits != 3072 && bits != 4096) return Fail<RsaPrivateKey>(CryptoErrc::kInvalidArgument);
  Pkey k(EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", static_cast<std::size_t>(bits)));
  if (!k) return Fail<RsaPrivateKey>(CryptoErrc::kRuntimeFault);
  return RsaPrivateKey(std::unique_ptr<Impl>(new Impl{std::move(k)}));
}

Res<RsaPrivateKey> RsaPrivateKey::FromPkcs8Der(ByteView der) {
  if (der.empty() || !FitsInt(der.size())) return Fail<RsaPrivateKey>(CryptoErrc::kInvalidArgument);
  unsigned char const *p = der.data();
  Pkey k(d2i_AutoPrivateKey(nullptr, &p, static_cast<long>(der.size())));
  if (!k) return Fail<RsaPrivateKey>(CryptoErrc::kInvalidArgument);
  if (!IsRsa(k.get())) return Fail<RsaPrivateKey>(CryptoErrc::kUnsupported);
  return RsaPrivateKey(std::unique_ptr<Impl>(new Impl{std::move(k)}));
}

Res<ByteVector> RsaPrivateKey::ToPkcs8Der() const {
  Bio bio(BIO_new(BIO_s_mem()));
  if (!bio || i2d_PKCS8PrivateKey_bio(bio.get(), impl_->key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  char *data = nullptr;
  long const len = BIO_get_mem_data(bio.get(), &data);
  if (len <= 0) return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  ByteVector out(reinterpret_cast<std::uint8_t *>(data), reinterpret_cast<std::uint8_t *>(data) + len);
  OPENSSL_cleanse(data, static_cast<std::size_t>(len));
  return out;
}

Res<RsaPublicKey> RsaPrivateKey::GetPublicKey() const {
  unsigned char *der = nullptr;
  int const n = i2d_PUBKEY(impl_->key.get(), &der);
  if (n <= 0) return Fail<RsaPublicKey>(CryptoErrc::kRuntimeFault);
  auto r = RsaPublicKey::FromDer(ByteView(der, static_cast<std::size_t>(n)));
  OPENSSL_free(der);
  return r;
}

Res<ByteVector> RsaPrivateKey::SignPss(ByteView message) const { return RsaSign(impl_->key.get(), message); }
Res<ByteVector> RsaPrivateKey::DecryptOaep(ByteView ct) const { return RsaCrypt(false, impl_->key.get(), ct); }

Res<RsaPublicKey> RsaPublicKey::FromDer(ByteView spki) {
  if (spki.empty() || !FitsInt(spki.size())) return Fail<RsaPublicKey>(CryptoErrc::kInvalidArgument);
  unsigned char const *p = spki.data();
  Pkey k(d2i_PUBKEY(nullptr, &p, static_cast<long>(spki.size())));
  if (!k) return Fail<RsaPublicKey>(CryptoErrc::kInvalidArgument);
  if (!IsRsa(k.get())) return Fail<RsaPublicKey>(CryptoErrc::kUnsupported);
  return RsaPublicKey(std::unique_ptr<Impl>(new Impl{std::move(k)}));
}

Res<ByteVector> RsaPublicKey::ToDer() const {
  unsigned char *der = nullptr;
  int const n = i2d_PUBKEY(impl_->key.get(), &der);
  if (n <= 0) return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  ByteVector out(der, der + n);
  OPENSSL_free(der);
  return out;
}

std::size_t RsaPublicKey::ModulusBits() const noexcept {
  int const b = EVP_PKEY_bits(impl_->key.get());
  return b > 0 ? static_cast<std::size_t>(b) : 0;
}

Res<bool> RsaPublicKey::VerifyPss(ByteView message, ByteView signature) const {
  auto d = Hash(HashAlgorithm::kSha256, message);
  if (!d.HasValue()) return Res<bool>::FromError(d.Error());
  PkeyCtx ctx(EVP_PKEY_CTX_new(impl_->key.get(), nullptr));
  if (!ctx) return Fail<bool>(CryptoErrc::kRuntimeFault);
  if (EVP_PKEY_verify_init(ctx.get()) != 1 || EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_PSS_PADDING) != 1 ||
      EVP_PKEY_CTX_set_rsa_pss_saltlen(ctx.get(), RSA_PSS_SALTLEN_DIGEST) != 1 ||
      EVP_PKEY_CTX_set_signature_md(ctx.get(), EVP_sha256()) != 1)
    return Fail<bool>(CryptoErrc::kRuntimeFault);
  int const rc = EVP_PKEY_verify(ctx.get(), signature.data(), signature.size(), d.Value().data(), d.Value().size());
  ERR_clear_error();
  return rc == 1;
}

Res<ByteVector> RsaPublicKey::EncryptOaep(ByteView plaintext) const {
  return RsaCrypt(true, impl_->key.get(), plaintext);
}

// --------------------------------------------------------------------- X.509
struct X509Certificate::Impl { X509Ptr cert; };

X509Certificate::X509Certificate(std::shared_ptr<Impl> i) noexcept : impl_(std::move(i)) {}
X509Certificate::X509Certificate(X509Certificate &&) noexcept = default;
X509Certificate &X509Certificate::operator=(X509Certificate &&) noexcept = default;
X509Certificate::X509Certificate(X509Certificate const &) = default;
X509Certificate &X509Certificate::operator=(X509Certificate const &) = default;
X509Certificate::~X509Certificate() = default;

namespace {
std::string NameOf(X509_NAME const *n) {
  Bio bio(BIO_new(BIO_s_mem()));
  if (!bio || n == nullptr) return {};
  X509_NAME_print_ex(bio.get(), n, 0, XN_FLAG_RFC2253);
  char *data = nullptr;
  long const len = BIO_get_mem_data(bio.get(), &data);
  return len > 0 ? std::string(data, static_cast<std::size_t>(len)) : std::string{};
}
std::time_t TimeOf(ASN1_TIME const *t) noexcept {
  if (t == nullptr) return 0;
  struct tm tmv{};
  if (ASN1_TIME_to_tm(t, &tmv) != 1) return 0;
  return timegm(&tmv);
}
Res<X509Certificate> Wrap(X509 *raw) {
  X509Ptr p(raw);
  if (!p) return Fail<X509Certificate>(CryptoErrc::kInvalidArgument);
  return X509Certificate(std::make_shared<X509Certificate::Impl>(X509Certificate::Impl{std::move(p)}));
}
X509 *Raw(X509Certificate const &c) { return c.RawImpl()->cert.get(); }
} // namespace

Res<X509Certificate> X509Certificate::FromDer(ByteView der) {
  if (der.empty() || !FitsInt(der.size())) return Fail<X509Certificate>(CryptoErrc::kInvalidArgument);
  unsigned char const *p = der.data();
  return Wrap(d2i_X509(nullptr, &p, static_cast<long>(der.size())));
}

Res<X509Certificate> X509Certificate::FromPem(std::string const &pem) {
  if (pem.empty() || !FitsInt(pem.size())) return Fail<X509Certificate>(CryptoErrc::kInvalidArgument);
  Bio bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())));
  if (!bio) return Fail<X509Certificate>(CryptoErrc::kRuntimeFault);
  return Wrap(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
}

Res<ByteVector> X509Certificate::ToDer() const {
  unsigned char *der = nullptr;
  int const n = i2d_X509(Raw(*this), &der);
  if (n <= 0) return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  ByteVector out(der, der + n);
  OPENSSL_free(der);
  return out;
}

Res<std::string> X509Certificate::ToPem() const {
  Bio bio(BIO_new(BIO_s_mem()));
  if (!bio || PEM_write_bio_X509(bio.get(), Raw(*this)) != 1) return Fail<std::string>(CryptoErrc::kRuntimeFault);
  char *data = nullptr;
  long const len = BIO_get_mem_data(bio.get(), &data);
  return std::string(data, static_cast<std::size_t>(len));
}

std::string X509Certificate::SubjectName() const { return NameOf(X509_get_subject_name(Raw(*this))); }
std::string X509Certificate::IssuerName() const { return NameOf(X509_get_issuer_name(Raw(*this))); }

std::string X509Certificate::SerialNumberHex() const {
  std::unique_ptr<BIGNUM, BnDel> bn(ASN1_INTEGER_to_BN(X509_get0_serialNumber(Raw(*this)), nullptr));
  if (!bn) return {};
  char *hex = BN_bn2hex(bn.get());
  std::string out = hex != nullptr ? hex : "";
  OPENSSL_free(hex);
  return out;
}

std::time_t X509Certificate::NotBefore() const noexcept { return TimeOf(X509_get0_notBefore(Raw(*this))); }
std::time_t X509Certificate::NotAfter() const noexcept { return TimeOf(X509_get0_notAfter(Raw(*this))); }

bool X509Certificate::IsCa() const noexcept {
  X509_check_ca(Raw(*this)); // populates cached extension flags
  return (X509_get_extension_flags(Raw(*this)) & EXFLAG_CA) != 0;
}

bool X509Certificate::IsSelfIssued() const noexcept {
  return X509_check_issued(Raw(*this), Raw(*this)) == X509_V_OK;
}

Res<ByteVector> X509Certificate::GetPublicKeyDer() const {
  EVP_PKEY *k = X509_get0_pubkey(Raw(*this));
  if (k == nullptr) return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  unsigned char *der = nullptr;
  int const n = i2d_PUBKEY(k, &der);
  if (n <= 0) return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  ByteVector out(der, der + n);
  OPENSSL_free(der);
  return out;
}

Res<bool> X509Certificate::IsSignedBy(ByteView issuerPublicKeyDer) const {
  if (issuerPublicKeyDer.empty() || !FitsInt(issuerPublicKeyDer.size())) return Fail<bool>(CryptoErrc::kInvalidArgument);
  unsigned char const *p = issuerPublicKeyDer.data();
  Pkey k(d2i_PUBKEY(nullptr, &p, static_cast<long>(issuerPublicKeyDer.size())));
  if (!k) return Fail<bool>(CryptoErrc::kInvalidArgument);
  int const rc = X509_verify(Raw(*this), k.get());
  ERR_clear_error();
  return rc == 1;
}

Res<ByteVector> X509Certificate::Fingerprint() const {
  auto der = ToDer();
  if (!der.HasValue()) return der;
  return Hash(HashAlgorithm::kSha256, der.Value());
}

Res<X509Certificate> IssueCertificate(CertificateRequest const &req, X509Certificate const *issuer,
                                      PrivateKey const &issuerKey) {
  if (req.commonName.empty() || req.commonName.size() > 64 || req.subjectPublicKeyDer.empty())
    return Fail<X509Certificate>(CryptoErrc::kInvalidArgument);
  // rebuild the EVP keys from DER (the PrivateKey internals are private)
  unsigned char const *sp = req.subjectPublicKeyDer.data();
  Pkey subjectKey(d2i_PUBKEY(nullptr, &sp, static_cast<long>(req.subjectPublicKeyDer.size())));
  if (!subjectKey) return Fail<X509Certificate>(CryptoErrc::kInvalidArgument);
  auto pkcs8 = issuerKey.ToPkcs8Der();
  if (!pkcs8.HasValue()) return Res<X509Certificate>::FromError(pkcs8.Error());
  unsigned char const *kp = pkcs8.Value().data();
  Pkey signKey(d2i_AutoPrivateKey(nullptr, &kp, static_cast<long>(pkcs8.Value().size())));
  SecureZero(pkcs8.Value().data(), pkcs8.Value().size());
  if (!signKey) return Fail<X509Certificate>(CryptoErrc::kRuntimeFault);

  X509Ptr x(X509_new());
  if (!x) return Fail<X509Certificate>(CryptoErrc::kRuntimeFault);
  X509_set_version(x.get(), 2);
  ASN1_INTEGER_set_uint64(X509_get_serialNumber(x.get()), req.serial);
  std::time_t nb = req.notBefore != 0 ? req.notBefore : std::time(nullptr) - 60;
  std::time_t na = req.notAfter != 0 ? req.notAfter : nb + 365L * 24 * 3600;
  if (na <= nb) return Fail<X509Certificate>(CryptoErrc::kInvalidArgument);
  if (X509_time_adj(X509_getm_notBefore(x.get()), 0, &nb) == nullptr ||
      X509_time_adj(X509_getm_notAfter(x.get()), 0, &na) == nullptr)
    return Fail<X509Certificate>(CryptoErrc::kRuntimeFault);
  X509_NAME *subj = X509_get_subject_name(x.get());
  if (X509_NAME_add_entry_by_txt(subj, "CN", MBSTRING_UTF8,
                                 reinterpret_cast<unsigned char const *>(req.commonName.data()),
                                 static_cast<int>(req.commonName.size()), -1, 0) != 1)
    return Fail<X509Certificate>(CryptoErrc::kRuntimeFault);
  if (issuer == nullptr) {
    X509_set_issuer_name(x.get(), subj);
  } else {
    X509_set_issuer_name(x.get(), X509_get_subject_name(Raw(*issuer)));
  }
  X509_set_pubkey(x.get(), subjectKey.get());

  X509V3_CTX v3;
  X509V3_set_ctx_nodb(&v3);
  X509V3_set_ctx(&v3, issuer != nullptr ? Raw(*issuer) : x.get(), x.get(), nullptr, nullptr, 0);
  auto addExt = [&](int nid, char const *value) {
    X509_EXTENSION *e = X509V3_EXT_conf_nid(nullptr, &v3, nid, value);
    if (e == nullptr) return false;
    int const rc = X509_add_ext(x.get(), e, -1);
    X509_EXTENSION_free(e);
    return rc == 1;
  };
  if (!addExt(NID_basic_constraints, req.isCa ? "critical,CA:TRUE" : "critical,CA:FALSE") ||
      !addExt(NID_key_usage, req.isCa ? "critical,keyCertSign,cRLSign" : "critical,digitalSignature") ||
      !addExt(NID_subject_key_identifier, "hash"))
    return Fail<X509Certificate>(CryptoErrc::kRuntimeFault);
  if (issuer != nullptr && !addExt(NID_authority_key_identifier, "keyid"))
    return Fail<X509Certificate>(CryptoErrc::kRuntimeFault);

  EVP_MD const *md = EVP_PKEY_base_id(signKey.get()) == EVP_PKEY_ED25519 ? nullptr : EVP_sha256();
  if (X509_sign(x.get(), signKey.get(), md) <= 0) return Fail<X509Certificate>(CryptoErrc::kRuntimeFault);
  return X509Certificate(std::make_shared<X509Certificate::Impl>(X509Certificate::Impl{std::move(x)}));
}

Res<void> X509Provider::AddTrustAnchor(X509Certificate const &anchor) {
  if (!anchor.IsCa()) return FailV(CryptoErrc::kInvalidArgument);
  std::lock_guard<std::mutex> lk(mutex_);
  anchors_.push_back(anchor);
  return Res<void>::FromValue();
}

void X509Provider::ClearTrustAnchors() {
  std::lock_guard<std::mutex> lk(mutex_);
  anchors_.clear();
}

std::size_t X509Provider::TrustAnchorCount() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return anchors_.size();
}

Res<void> X509Provider::VerifyChain(X509Certificate const &leaf, std::vector<X509Certificate> const &intermediates,
                                    std::time_t at) const {
  std::unique_ptr<X509_STORE, StoreDel> store(X509_STORE_new());
  if (!store) return FailV(CryptoErrc::kRuntimeFault);
  {
    std::lock_guard<std::mutex> lk(mutex_);
    if (anchors_.empty()) return FailV(CryptoErrc::kEmptyContainer);
    for (auto const &a : anchors_)
      if (X509_STORE_add_cert(store.get(), Raw(a)) != 1) return FailV(CryptoErrc::kRuntimeFault);
  }
  STACK_OF(X509) *inter = sk_X509_new_null();
  if (inter == nullptr) return FailV(CryptoErrc::kRuntimeFault);
  for (auto const &c : intermediates) {
    X509_up_ref(Raw(c));
    sk_X509_push(inter, Raw(c));
  }
  std::unique_ptr<X509_STORE_CTX, StoreCtxDel> ctx(X509_STORE_CTX_new());
  bool ok = ctx && X509_STORE_CTX_init(ctx.get(), store.get(), Raw(leaf), inter) == 1;
  int err = 0;
  if (ok) {
    X509_STORE_CTX_set_time(ctx.get(), 0, at != 0 ? at : std::time(nullptr));
    ok = X509_verify_cert(ctx.get()) == 1;
    if (!ok) err = X509_STORE_CTX_get_error(ctx.get());
  }
  ctx.reset();
  sk_X509_pop_free(inter, X509_free);
  ERR_clear_error();
  if (ok) return Res<void>::FromValue();
  return Res<void>::FromError(MakeErrorCode(CryptoErrc::kAuthTagNotValid, err));
}

} // namespace ara::crypto
