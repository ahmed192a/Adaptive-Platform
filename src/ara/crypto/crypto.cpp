#include "ara/crypto/crypto.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

#include <limits>

namespace ara::crypto {
namespace {

template <typename T> Res<T> Fail(CryptoErrc e) { return Res<T>::FromError(MakeErrorCode(e)); }

EVP_MD const *MdFor(HashAlgorithm a) noexcept {
  switch (a) {
  case HashAlgorithm::kSha256: return EVP_sha256();
  case HashAlgorithm::kSha384: return EVP_sha384();
  case HashAlgorithm::kSha512: return EVP_sha512();
  }
  return nullptr;
}

bool FitsInt(std::size_t n) { return n <= static_cast<std::size_t>(std::numeric_limits<int>::max()); }

struct MdCtxDel { void operator()(EVP_MD_CTX *c) const { EVP_MD_CTX_free(c); } };
struct CipherCtxDel { void operator()(EVP_CIPHER_CTX *c) const { EVP_CIPHER_CTX_free(c); } };
struct PkeyDel { void operator()(EVP_PKEY *k) const { EVP_PKEY_free(k); } };
struct PkeyCtxDel { void operator()(EVP_PKEY_CTX *c) const { EVP_PKEY_CTX_free(c); } };
using MdCtx = std::unique_ptr<EVP_MD_CTX, MdCtxDel>;
using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, CipherCtxDel>;
using Pkey = std::unique_ptr<EVP_PKEY, PkeyDel>;
using PkeyCtx = std::unique_ptr<EVP_PKEY_CTX, PkeyCtxDel>;

} // namespace

bool ConstantTimeEquals(ByteView a, ByteView b) noexcept {
  if (a.size() != b.size()) return false;
  if (a.size() == 0) return true;
  return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

void SecureZero(void *p, std::size_t n) noexcept {
  if (p != nullptr && n != 0) OPENSSL_cleanse(p, n);
}

// ---------------------------------------------------------------- random
Res<ByteVector> GenerateRandom(std::size_t size) {
  if (!FitsInt(size)) return Fail<ByteVector>(CryptoErrc::kInvalidArgument);
  ByteVector out(size);
  if (size != 0 && RAND_bytes(out.data(), static_cast<int>(size)) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  return out;
}

// ------------------------------------------------------------------ hash
std::size_t DigestSize(HashAlgorithm alg) noexcept {
  switch (alg) {
  case HashAlgorithm::kSha256: return 32;
  case HashAlgorithm::kSha384: return 48;
  case HashAlgorithm::kSha512: return 64;
  }
  return 0;
}

struct HashFunction::Impl {
  HashAlgorithm alg;
  MdCtx ctx;
};
HashFunction::HashFunction(std::unique_ptr<Impl> i) : impl_(std::move(i)) {}
HashFunction::HashFunction(HashFunction &&) noexcept = default;
HashFunction &HashFunction::operator=(HashFunction &&) noexcept = default;
HashFunction::~HashFunction() = default;

Res<HashFunction> HashFunction::Create(HashAlgorithm alg) {
  auto const *md = MdFor(alg);
  if (md == nullptr) return Fail<HashFunction>(CryptoErrc::kUnsupported);
  MdCtx ctx(EVP_MD_CTX_new());
  if (!ctx || EVP_DigestInit_ex(ctx.get(), md, nullptr) != 1)
    return Fail<HashFunction>(CryptoErrc::kRuntimeFault);
  return HashFunction(std::unique_ptr<Impl>(new Impl{alg, std::move(ctx)}));
}

Res<void> HashFunction::Update(ByteView data) {
  if (!impl_) return Res<void>::FromError(MakeErrorCode(CryptoErrc::kUninitializedContext));
  if (!data.empty() && EVP_DigestUpdate(impl_->ctx.get(), data.data(), data.size()) != 1)
    return Res<void>::FromError(MakeErrorCode(CryptoErrc::kRuntimeFault));
  return {};
}

Res<ByteVector> HashFunction::Finish() {
  if (!impl_) return Fail<ByteVector>(CryptoErrc::kUninitializedContext);
  ByteVector out(EVP_MAX_MD_SIZE);
  unsigned len = 0;
  if (EVP_DigestFinal_ex(impl_->ctx.get(), out.data(), &len) != 1 ||
      EVP_DigestInit_ex(impl_->ctx.get(), MdFor(impl_->alg), nullptr) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  out.resize(len);
  return out;
}

Res<ByteVector> Hash(HashAlgorithm alg, ByteView data) {
  auto h = HashFunction::Create(alg);
  if (!h.HasValue()) return Res<ByteVector>::FromError(h.Error());
  auto u = h.Value().Update(data);
  if (!u.HasValue()) return Res<ByteVector>::FromError(u.Error());
  return h.Value().Finish();
}

// ------------------------------------------------------------- SecretKey
SecretKey::SecretKey(SecretKey &&o) noexcept
    : material_(std::move(o.material_)), usage_(o.usage_), exportable_(o.exportable_) {
  o.material_.clear();
  o.usage_ = 0;
}
SecretKey &SecretKey::operator=(SecretKey &&o) noexcept {
  if (this != &o) {
    SecureZero(material_.data(), material_.size());
    material_ = std::move(o.material_);
    usage_ = o.usage_;
    exportable_ = o.exportable_;
    o.material_.clear();
    o.usage_ = 0;
  }
  return *this;
}
SecretKey::~SecretKey() { SecureZero(material_.data(), material_.size()); }

Res<SecretKey> SecretKey::Import(ByteView material, std::uint32_t usage, bool exportable) {
  if (material.empty()) return Fail<SecretKey>(CryptoErrc::kEmptyContainer);
  if ((usage & ~static_cast<std::uint32_t>(kUsageAll)) != 0 || usage == 0)
    return Fail<SecretKey>(CryptoErrc::kInvalidArgument);
  SecretKey k;
  k.material_.assign(material.data(), material.data() + material.size());
  k.usage_ = usage;
  k.exportable_ = exportable;
  return k;
}

Res<SecretKey> SecretKey::Generate(std::size_t sizeBytes, std::uint32_t usage, bool exportable) {
  auto r = GenerateRandom(sizeBytes);
  if (!r.HasValue()) return Res<SecretKey>::FromError(r.Error());
  auto k = Import(r.Value(), usage, exportable);
  SecureZero(r.Value().data(), r.Value().size());
  return k;
}

Res<ByteVector> SecretKey::Export() const {
  if (!exportable_) return Fail<ByteVector>(CryptoErrc::kKeyNotExportable);
  return material_;
}

// ------------------------------------------------------------------ HMAC
struct MessageAuthnCode::Impl {
  HashAlgorithm alg;
  MdCtx ctx;
  Pkey key;
};
MessageAuthnCode::MessageAuthnCode(std::unique_ptr<Impl> i) : impl_(std::move(i)) {}
MessageAuthnCode::MessageAuthnCode(MessageAuthnCode &&) noexcept = default;
MessageAuthnCode &MessageAuthnCode::operator=(MessageAuthnCode &&) noexcept = default;
MessageAuthnCode::~MessageAuthnCode() = default;

Res<MessageAuthnCode> MessageAuthnCode::Create(HashAlgorithm alg, SecretKey const &key) {
  if ((key.usage_ & kUsageMac) == 0) return Fail<MessageAuthnCode>(CryptoErrc::kUsageViolation);
  if (!key.IsValid() || !FitsInt(key.material_.size()))
    return Fail<MessageAuthnCode>(CryptoErrc::kInvalidKeyLength);
  auto const *md = MdFor(alg);
  if (md == nullptr) return Fail<MessageAuthnCode>(CryptoErrc::kUnsupported);
  Pkey pk(EVP_PKEY_new_raw_private_key(EVP_PKEY_HMAC, nullptr, key.material_.data(),
                                       key.material_.size()));
  MdCtx ctx(EVP_MD_CTX_new());
  if (!pk || !ctx || EVP_DigestSignInit(ctx.get(), nullptr, md, nullptr, pk.get()) != 1)
    return Fail<MessageAuthnCode>(CryptoErrc::kRuntimeFault);
  return MessageAuthnCode(std::unique_ptr<Impl>(new Impl{alg, std::move(ctx), std::move(pk)}));
}

Res<void> MessageAuthnCode::Update(ByteView data) {
  if (!impl_) return Res<void>::FromError(MakeErrorCode(CryptoErrc::kUninitializedContext));
  if (!data.empty() && EVP_DigestSignUpdate(impl_->ctx.get(), data.data(), data.size()) != 1)
    return Res<void>::FromError(MakeErrorCode(CryptoErrc::kRuntimeFault));
  return {};
}

Res<ByteVector> MessageAuthnCode::Finish() {
  if (!impl_) return Fail<ByteVector>(CryptoErrc::kUninitializedContext);
  std::size_t len = 0;
  if (EVP_DigestSignFinal(impl_->ctx.get(), nullptr, &len) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  ByteVector tag(len);
  if (EVP_DigestSignFinal(impl_->ctx.get(), tag.data(), &len) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  tag.resize(len);
  // re-arm with the same key: DigestSignFinal consumed the context
  if (EVP_DigestSignInit(impl_->ctx.get(), nullptr, MdFor(impl_->alg), nullptr,
                         impl_->key.get()) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  return tag;
}

Res<bool> MessageAuthnCode::Verify(ByteView data, ByteView tag) {
  auto u = Update(data);
  if (!u.HasValue()) return Res<bool>::FromError(u.Error());
  auto t = Finish();
  if (!t.HasValue()) return Res<bool>::FromError(t.Error());
  return ConstantTimeEquals(t.Value(), tag);
}

// --------------------------------------------------------------- AES-GCM
namespace {
EVP_CIPHER const *GcmFor(SecretKey const &k) {
  switch (k.Size()) {
  case 16: return EVP_aes_128_gcm();
  case 32: return EVP_aes_256_gcm();
  default: return nullptr;
  }
}
} // namespace

Res<ByteVector> AeadEncrypt(SecretKey const &key, ByteView nonce, ByteView aad, ByteView pt) {
  if ((key.usage_ & kUsageEncrypt) == 0) return Fail<ByteVector>(CryptoErrc::kUsageViolation);
  auto const *cipher = GcmFor(key);
  if (cipher == nullptr) return Fail<ByteVector>(CryptoErrc::kInvalidKeyLength);
  if (nonce.size() != kGcmNonceSize || !FitsInt(pt.size()) || !FitsInt(aad.size()))
    return Fail<ByteVector>(CryptoErrc::kInvalidArgument);
  CipherCtx ctx(EVP_CIPHER_CTX_new());
  if (!ctx || EVP_EncryptInit_ex(ctx.get(), cipher, nullptr, key.material_.data(), nonce.data()) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  int n = 0;
  if (!aad.empty() &&
      EVP_EncryptUpdate(ctx.get(), nullptr, &n, aad.data(), static_cast<int>(aad.size())) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  ByteVector out(pt.size() + kGcmTagSize);
  int total = 0;
  if (!pt.empty()) {
    if (EVP_EncryptUpdate(ctx.get(), out.data(), &n, pt.data(), static_cast<int>(pt.size())) != 1)
      return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
    total = n;
  }
  if (EVP_EncryptFinal_ex(ctx.get(), out.data() + total, &n) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  total += n;
  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(kGcmTagSize),
                          out.data() + pt.size()) != 1 ||
      static_cast<std::size_t>(total) != pt.size())
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  return out;
}

Res<ByteVector> AeadDecrypt(SecretKey const &key, ByteView nonce, ByteView aad, ByteView sealed) {
  if ((key.usage_ & kUsageDecrypt) == 0) return Fail<ByteVector>(CryptoErrc::kUsageViolation);
  auto const *cipher = GcmFor(key);
  if (cipher == nullptr) return Fail<ByteVector>(CryptoErrc::kInvalidKeyLength);
  if (nonce.size() != kGcmNonceSize || !FitsInt(sealed.size()) || !FitsInt(aad.size()))
    return Fail<ByteVector>(CryptoErrc::kInvalidArgument);
  if (sealed.size() < kGcmTagSize) return Fail<ByteVector>(CryptoErrc::kInvalidInputSize);
  std::size_t const ctLen = sealed.size() - kGcmTagSize;
  CipherCtx ctx(EVP_CIPHER_CTX_new());
  if (!ctx || EVP_DecryptInit_ex(ctx.get(), cipher, nullptr, key.material_.data(), nonce.data()) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  int n = 0;
  if (!aad.empty() &&
      EVP_DecryptUpdate(ctx.get(), nullptr, &n, aad.data(), static_cast<int>(aad.size())) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  ByteVector out(ctLen);
  int total = 0;
  if (ctLen != 0) {
    if (EVP_DecryptUpdate(ctx.get(), out.data(), &n, sealed.data(), static_cast<int>(ctLen)) != 1)
      return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
    total = n;
  }
  std::uint8_t tag[kGcmTagSize];
  std::copy(sealed.data() + ctLen, sealed.data() + sealed.size(), tag);
  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(kGcmTagSize), tag) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  if (EVP_DecryptFinal_ex(ctx.get(), out.data() + total, &n) != 1) {
    SecureZero(out.data(), out.size()); // never release unauthenticated plaintext
    return Fail<ByteVector>(CryptoErrc::kAuthTagNotValid);
  }
  return out;
}

Res<ByteVector> AeadSeal(SecretKey const &key, ByteView aad, ByteView plaintext) {
  auto nonce = GenerateRandom(kGcmNonceSize);
  if (!nonce.HasValue()) return Res<ByteVector>::FromError(nonce.Error());
  auto ct = AeadEncrypt(key, nonce.Value(), aad, plaintext);
  if (!ct.HasValue()) return ct;
  ByteVector out = std::move(nonce).Value();
  out.insert(out.end(), ct.Value().begin(), ct.Value().end());
  return out;
}

Res<ByteVector> AeadOpen(SecretKey const &key, ByteView aad, ByteView sealed) {
  if (sealed.size() < kGcmNonceSize + kGcmTagSize) return Fail<ByteVector>(CryptoErrc::kInvalidInputSize);
  return AeadDecrypt(key, ByteView(sealed.data(), kGcmNonceSize), aad,
                     ByteView(sealed.data() + kGcmNonceSize, sealed.size() - kGcmNonceSize));
}

// ------------------------------------------------------------ signatures
struct PublicKey::Impl { Pkey key; };
struct PrivateKey::Impl { Pkey key; };

PublicKey::PublicKey(std::unique_ptr<Impl> i, SignatureAlgorithm a) : impl_(std::move(i)), alg_(a) {}
PublicKey::PublicKey(PublicKey &&) noexcept = default;
PublicKey &PublicKey::operator=(PublicKey &&) noexcept = default;
PublicKey::~PublicKey() = default;
PrivateKey::PrivateKey(std::unique_ptr<Impl> i, SignatureAlgorithm a) : impl_(std::move(i)), alg_(a) {}
PrivateKey::PrivateKey(PrivateKey &&) noexcept = default;
PrivateKey &PrivateKey::operator=(PrivateKey &&) noexcept = default;
PrivateKey::~PrivateKey() = default;

namespace {
EVP_MD const *SigMd(SignatureAlgorithm a) { return a == SignatureAlgorithm::kEd25519 ? nullptr : EVP_sha256(); }
} // namespace

Res<PrivateKey> PrivateKey::Generate(SignatureAlgorithm alg) {
  EVP_PKEY *raw = nullptr;
  if (alg == SignatureAlgorithm::kEd25519) {
    raw = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
  } else {
    raw = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
  }
  Pkey k(raw);
  if (!k) return Fail<PrivateKey>(CryptoErrc::kRuntimeFault);
  return PrivateKey(std::unique_ptr<Impl>(new Impl{std::move(k)}), alg);
}

Res<ByteVector> PrivateKey::Sign(ByteView message) const {
  if (!impl_) return Fail<ByteVector>(CryptoErrc::kUninitializedContext);
  MdCtx ctx(EVP_MD_CTX_new());
  if (!ctx || EVP_DigestSignInit(ctx.get(), nullptr, SigMd(alg_), nullptr, impl_->key.get()) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  std::size_t len = 0;
  if (EVP_DigestSign(ctx.get(), nullptr, &len, message.data(), message.size()) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  ByteVector sig(len);
  if (EVP_DigestSign(ctx.get(), sig.data(), &len, message.data(), message.size()) != 1)
    return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  sig.resize(len);
  return sig;
}

Res<PublicKey> PrivateKey::GetPublicKey() const {
  if (!impl_) return Fail<PublicKey>(CryptoErrc::kUninitializedContext);
  int const len = i2d_PUBKEY(impl_->key.get(), nullptr);
  if (len <= 0) return Fail<PublicKey>(CryptoErrc::kRuntimeFault);
  ByteVector der(static_cast<std::size_t>(len));
  unsigned char *p = der.data();
  if (i2d_PUBKEY(impl_->key.get(), &p) != len) return Fail<PublicKey>(CryptoErrc::kRuntimeFault);
  return PublicKey::FromDer(der);
}

Res<PublicKey> PublicKey::FromDer(ByteView der) {
  if (der.empty() || !FitsInt(der.size())) return Fail<PublicKey>(CryptoErrc::kInvalidArgument);
  unsigned char const *p = der.data();
  Pkey k(d2i_PUBKEY(nullptr, &p, static_cast<long>(der.size())));
  if (!k) return Fail<PublicKey>(CryptoErrc::kInvalidArgument);
  SignatureAlgorithm alg;
  if (EVP_PKEY_id(k.get()) == EVP_PKEY_ED25519) alg = SignatureAlgorithm::kEd25519;
  else if (EVP_PKEY_id(k.get()) == EVP_PKEY_EC) alg = SignatureAlgorithm::kEcdsaP256Sha256;
  else return Fail<PublicKey>(CryptoErrc::kUnsupported);
  return PublicKey(std::unique_ptr<Impl>(new Impl{std::move(k)}), alg);
}

Res<ByteVector> PublicKey::ToDer() const {
  if (!impl_) return Fail<ByteVector>(CryptoErrc::kUninitializedContext);
  int const len = i2d_PUBKEY(impl_->key.get(), nullptr);
  if (len <= 0) return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  ByteVector der(static_cast<std::size_t>(len));
  unsigned char *p = der.data();
  if (i2d_PUBKEY(impl_->key.get(), &p) != len) return Fail<ByteVector>(CryptoErrc::kRuntimeFault);
  return der;
}

Res<bool> PublicKey::Verify(ByteView message, ByteView sig) const {
  if (!impl_) return Fail<bool>(CryptoErrc::kUninitializedContext);
  MdCtx ctx(EVP_MD_CTX_new());
  if (!ctx || EVP_DigestVerifyInit(ctx.get(), nullptr, SigMd(alg_), nullptr, impl_->key.get()) != 1)
    return Fail<bool>(CryptoErrc::kRuntimeFault);
  return EVP_DigestVerify(ctx.get(), sig.data(), sig.size(), message.data(), message.size()) == 1;
}

// --------------------------------------------------------------- KeyStore
Res<void> KeyStore::Store(std::string const &slot, SecretKey key) {
  if (slot.empty() || !key.IsValid())
    return Res<void>::FromError(MakeErrorCode(CryptoErrc::kInvalidArgument));
  std::lock_guard<std::mutex> lk(mutex_);
  slots_[slot] = std::move(key);
  return {};
}
Res<void> KeyStore::Erase(std::string const &slot) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (slots_.erase(slot) == 0)
    return Res<void>::FromError(MakeErrorCode(CryptoErrc::kUnknownIdentifier));
  return {};
}
bool KeyStore::Contains(std::string const &slot) const {
  std::lock_guard<std::mutex> lk(mutex_);
  return slots_.count(slot) != 0;
}

} // namespace ara::crypto
