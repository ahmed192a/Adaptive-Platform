#include "ara/crypto/key_slot.h"

#include <algorithm>
#include <cctype>

namespace ara::crypto {
namespace {

template <typename T> Res<T> Fail(CryptoErrc e) { return Res<T>::FromError(MakeErrorCode(e)); }

bool IsSymmetric(SlotAlgorithm a) noexcept {
  return a == SlotAlgorithm::kAes256Gcm || a == SlotAlgorithm::kHmacSha256;
}

std::uint32_t AllowedUsage(SlotAlgorithm a) noexcept {
  switch (a) {
  case SlotAlgorithm::kAes256Gcm: return kSlotEncrypt | kSlotDecrypt;
  case SlotAlgorithm::kHmacSha256: return kSlotMac;
  case SlotAlgorithm::kEd25519:
  case SlotAlgorithm::kEcdsaP256: return kSlotSign | kSlotVerify;
  }
  return 0;
}

SignatureAlgorithm SigAlg(SlotAlgorithm a) noexcept {
  return a == SlotAlgorithm::kEd25519 ? SignatureAlgorithm::kEd25519 : SignatureAlgorithm::kEcdsaP256Sha256;
}

bool ValidName(std::string const &n) {
  if (n.empty() || n.size() > 64) return false;
  return std::all_of(n.begin(), n.end(), [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.' || c == '/' || c == '-';
  });
}

std::string ToHex(ByteVector const &b) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string s;
  s.reserve(b.size() * 2);
  for (auto v : b) {
    s.push_back(kDigits[v >> 4]);
    s.push_back(kDigits[v & 0xF]);
  }
  return s;
}

bool FromHex(std::string const &s, ByteVector &out) {
  if (s.size() % 2 != 0) return false;
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  out.clear();
  for (std::size_t i = 0; i < s.size(); i += 2) {
    int const hi = nib(s[i]);
    int const lo = nib(s[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
  }
  return true;
}

std::string StorageKey(std::string const &name) { return "keyslot/" + name; }

} // namespace

struct KeyStorageProvider::Slot {
  KeySlotPrototype proto;
  SlotState state{SlotState::kEmpty};
  std::variant<std::monostate, SecretKey, PrivateKey> key;
};

struct KeyStorageProvider::Impl {
  mutable std::mutex mutex;
  std::map<std::string, Slot> slots;
  ara::per::SharedHandle<ara::per::KeyValueStorage> storage;
  SecretKey wrap;

  // AAD binds the blob to the slot identity and its policy.
  static ByteVector Aad(KeySlotPrototype const &p) {
    std::string a = "AP-KEYSLOT-v1|" + p.name + "|" + std::to_string(static_cast<int>(p.algorithm)) + "|" +
                    std::to_string(p.usage) + "|" + (p.exportable ? "x" : "-");
    return ByteVector(a.begin(), a.end());
  }

  Res<void> Persist(Slot const &s, ByteVector const &plain) {
    if (!s.proto.persistent) return {};
    auto sealed = AeadSeal(wrap, Aad(s.proto), plain);
    if (!sealed.HasValue()) return Res<void>::FromError(sealed.Error());
    if (auto r = storage->SetValue(StorageKey(s.proto.name), ToHex(sealed.Value())); !r.HasValue())
      return Res<void>::FromError(MakeErrorCode(CryptoErrc::kRuntimeFault));
    if (auto r = storage->SyncToStorage(); !r.HasValue())
      return Res<void>::FromError(MakeErrorCode(CryptoErrc::kRuntimeFault));
    return {};
  }

  Res<void> Unpersist(Slot const &s) {
    if (!s.proto.persistent) return {};
    auto ex = storage->KeyExists(StorageKey(s.proto.name));
    if (ex.HasValue() && ex.Value()) {
      if (!storage->RemoveKey(StorageKey(s.proto.name)).HasValue() || !storage->SyncToStorage().HasValue())
        return Res<void>::FromError(MakeErrorCode(CryptoErrc::kRuntimeFault));
    }
    return {};
  }

  // Builds the in-memory key from plaintext material and installs it.
  static Res<void> Install(Slot &s, ByteView material) {
    if (IsSymmetric(s.proto.algorithm)) {
      if (s.proto.algorithm == SlotAlgorithm::kAes256Gcm && material.size() != 32)
        return Res<void>::FromError(MakeErrorCode(CryptoErrc::kInvalidKeyLength));
      if (s.proto.algorithm == SlotAlgorithm::kHmacSha256 && material.size() < 16)
        return Res<void>::FromError(MakeErrorCode(CryptoErrc::kInvalidKeyLength));
      auto k = SecretKey::Import(material, s.proto.usage & 0x7U, true);
      if (!k.HasValue()) return Res<void>::FromError(k.Error());
      s.key = std::move(k).Value();
    } else {
      auto k = PrivateKey::FromPkcs8Der(material);
      if (!k.HasValue()) return Res<void>::FromError(k.Error());
      if (k.Value().Algorithm() != SigAlg(s.proto.algorithm))
        return Res<void>::FromError(MakeErrorCode(CryptoErrc::kIncompatibleArguments));
      s.key = std::move(k).Value();
    }
    s.state = SlotState::kOccupied;
    return {};
  }

  void Load(Slot &s) {
    auto ex = storage->KeyExists(StorageKey(s.proto.name));
    if (!ex.HasValue() || !ex.Value()) return;
    auto hex = storage->GetValue<std::string>(StorageKey(s.proto.name));
    ByteVector blob;
    if (hex.HasValue() && FromHex(hex.Value(), blob)) {
      auto plain = AeadOpen(wrap, Aad(s.proto), blob);
      if (plain.HasValue()) {
        auto r = Install(s, plain.Value());
        SecureZero(plain.Value().data(), plain.Value().size());
        if (r.HasValue()) return;
      }
    }
    s.state = SlotState::kCorrupted;
  }

  // Finds the slot and checks it holds a usable key with the requested usage.
  Res<Slot *> Use(std::string const &name, std::uint32_t usage) {
    auto it = slots.find(name);
    if (it == slots.end()) return Fail<Slot *>(CryptoErrc::kUnknownIdentifier);
    Slot &s = it->second;
    if (s.state == SlotState::kCorrupted) return Fail<Slot *>(CryptoErrc::kAuthTagNotValid);
    if (s.state == SlotState::kEmpty) return Fail<Slot *>(CryptoErrc::kEmptyContainer);
    if ((s.proto.usage & usage) != usage) return Fail<Slot *>(CryptoErrc::kUsageViolation);
    return &s;
  }
};

KeyStorageProvider::KeyStorageProvider(std::unique_ptr<Impl> i) : impl_(std::move(i)) {}
KeyStorageProvider::KeyStorageProvider(KeyStorageProvider &&) noexcept = default;
KeyStorageProvider &KeyStorageProvider::operator=(KeyStorageProvider &&) noexcept = default;
KeyStorageProvider::~KeyStorageProvider() = default;

KeyStorageProvider KeyStorageProvider::CreateVolatile() {
  return KeyStorageProvider(std::make_unique<Impl>());
}

Res<KeyStorageProvider> KeyStorageProvider::Create(ara::per::SharedHandle<ara::per::KeyValueStorage> storage,
                                                   ByteView wrappingKey) {
  if (!storage) return Fail<KeyStorageProvider>(CryptoErrc::kInvalidArgument);
  if (wrappingKey.size() != 32) return Fail<KeyStorageProvider>(CryptoErrc::kInvalidKeyLength);
  auto k = SecretKey::Import(wrappingKey, kUsageEncrypt | kUsageDecrypt, false);
  if (!k.HasValue()) return Fail<KeyStorageProvider>(CryptoErrc::kRuntimeFault);
  auto impl = std::make_unique<Impl>();
  impl->storage = std::move(storage);
  impl->wrap = std::move(k).Value();
  return KeyStorageProvider(std::move(impl));
}

Res<void> KeyStorageProvider::DefineSlot(KeySlotPrototype const &proto) {
  auto bad = [](CryptoErrc e) { return Res<void>::FromError(MakeErrorCode(e)); };
  if (!ValidName(proto.name)) return bad(CryptoErrc::kInvalidArgument);
  if (proto.usage == 0 || (proto.usage & ~AllowedUsage(proto.algorithm)) != 0)
    return bad(CryptoErrc::kIncompatibleArguments);
  // Exporting a private signing key is not supported by this provider.
  if (proto.exportable && !IsSymmetric(proto.algorithm)) return bad(CryptoErrc::kUnsupported);
  std::lock_guard<std::mutex> lk(impl_->mutex);
  if (proto.persistent && !impl_->storage) return bad(CryptoErrc::kUnsupported);
  if (impl_->slots.count(proto.name) != 0) return bad(CryptoErrc::kInvalidArgument);
  Slot &s = impl_->slots[proto.name];
  s.proto = proto;
  if (proto.persistent) impl_->Load(s);
  return {};
}

Res<KeySlotInfo> KeyStorageProvider::GetSlotInfo(std::string const &name) const {
  std::lock_guard<std::mutex> lk(impl_->mutex);
  auto it = impl_->slots.find(name);
  if (it == impl_->slots.end()) return Fail<KeySlotInfo>(CryptoErrc::kUnknownIdentifier);
  return KeySlotInfo{it->second.proto, it->second.state};
}

std::vector<KeySlotInfo> KeyStorageProvider::ListSlots() const {
  std::lock_guard<std::mutex> lk(impl_->mutex);
  std::vector<KeySlotInfo> out;
  for (auto const &kv : impl_->slots) out.push_back({kv.second.proto, kv.second.state});
  return out;
}

Res<void> KeyStorageProvider::Generate(std::string const &name) {
  std::lock_guard<std::mutex> lk(impl_->mutex);
  auto it = impl_->slots.find(name);
  if (it == impl_->slots.end()) return Res<void>::FromError(MakeErrorCode(CryptoErrc::kUnknownIdentifier));
  Slot &s = it->second;
  ByteVector plain;
  if (IsSymmetric(s.proto.algorithm)) {
    auto r = GenerateRandom(32);
    if (!r.HasValue()) return Res<void>::FromError(r.Error());
    plain = std::move(r).Value();
  } else {
    auto k = PrivateKey::Generate(SigAlg(s.proto.algorithm));
    if (!k.HasValue()) return Res<void>::FromError(k.Error());
    auto der = k.Value().ToPkcs8Der();
    if (!der.HasValue()) return Res<void>::FromError(der.Error());
    plain = std::move(der).Value();
  }
  auto inst = Impl::Install(s, plain);
  Res<void> res = inst.HasValue() ? impl_->Persist(s, plain) : inst;
  SecureZero(plain.data(), plain.size());
  if (!res.HasValue()) {
    s.key = std::monostate{};
    s.state = SlotState::kEmpty;
  }
  return res;
}

Res<void> KeyStorageProvider::Import(std::string const &name, ByteView material) {
  std::lock_guard<std::mutex> lk(impl_->mutex);
  auto it = impl_->slots.find(name);
  if (it == impl_->slots.end()) return Res<void>::FromError(MakeErrorCode(CryptoErrc::kUnknownIdentifier));
  Slot &s = it->second;
  auto inst = Impl::Install(s, material);
  if (!inst.HasValue()) return inst;
  ByteVector plain(material.data(), material.data() + material.size());
  // Persist the canonical form so asymmetric imports are normalised.
  if (!IsSymmetric(s.proto.algorithm)) {
    SecureZero(plain.data(), plain.size());
    auto der = std::get<PrivateKey>(s.key).ToPkcs8Der();
    if (!der.HasValue()) return Res<void>::FromError(der.Error());
    plain = std::move(der).Value();
  }
  auto res = impl_->Persist(s, plain);
  SecureZero(plain.data(), plain.size());
  if (!res.HasValue()) {
    s.key = std::monostate{};
    s.state = SlotState::kEmpty;
  }
  return res;
}

Res<ByteVector> KeyStorageProvider::Export(std::string const &name) const {
  std::lock_guard<std::mutex> lk(impl_->mutex);
  auto s = impl_->Use(name, 0);
  if (!s.HasValue()) return Res<ByteVector>::FromError(s.Error());
  if (!s.Value()->proto.exportable) return Fail<ByteVector>(CryptoErrc::kKeyNotExportable);
  return std::get<SecretKey>(s.Value()->key).Export();
}

Res<void> KeyStorageProvider::Clear(std::string const &name) {
  std::lock_guard<std::mutex> lk(impl_->mutex);
  auto it = impl_->slots.find(name);
  if (it == impl_->slots.end()) return Res<void>::FromError(MakeErrorCode(CryptoErrc::kUnknownIdentifier));
  if (auto r = impl_->Unpersist(it->second); !r.HasValue()) return r;
  it->second.key = std::monostate{};
  it->second.state = SlotState::kEmpty;
  return {};
}

Res<ByteVector> KeyStorageProvider::Seal(std::string const &name, ByteView aad, ByteView plaintext) const {
  std::lock_guard<std::mutex> lk(impl_->mutex);
  auto s = impl_->Use(name, kSlotEncrypt);
  if (!s.HasValue()) return Res<ByteVector>::FromError(s.Error());
  if (s.Value()->proto.algorithm != SlotAlgorithm::kAes256Gcm) return Fail<ByteVector>(CryptoErrc::kIncompatibleArguments);
  return AeadSeal(std::get<SecretKey>(s.Value()->key), aad, plaintext);
}

Res<ByteVector> KeyStorageProvider::Open(std::string const &name, ByteView aad, ByteView sealed) const {
  std::lock_guard<std::mutex> lk(impl_->mutex);
  auto s = impl_->Use(name, kSlotDecrypt);
  if (!s.HasValue()) return Res<ByteVector>::FromError(s.Error());
  if (s.Value()->proto.algorithm != SlotAlgorithm::kAes256Gcm) return Fail<ByteVector>(CryptoErrc::kIncompatibleArguments);
  return AeadOpen(std::get<SecretKey>(s.Value()->key), aad, sealed);
}

Res<ByteVector> KeyStorageProvider::ComputeMac(std::string const &name, ByteView data) const {
  std::lock_guard<std::mutex> lk(impl_->mutex);
  auto s = impl_->Use(name, kSlotMac);
  if (!s.HasValue()) return Res<ByteVector>::FromError(s.Error());
  if (s.Value()->proto.algorithm != SlotAlgorithm::kHmacSha256) return Fail<ByteVector>(CryptoErrc::kIncompatibleArguments);
  auto mac = MessageAuthnCode::Create(HashAlgorithm::kSha256, std::get<SecretKey>(s.Value()->key));
  if (!mac.HasValue()) return Res<ByteVector>::FromError(mac.Error());
  if (auto u = mac.Value().Update(data); !u.HasValue()) return Res<ByteVector>::FromError(u.Error());
  return mac.Value().Finish();
}

Res<bool> KeyStorageProvider::VerifyMac(std::string const &name, ByteView data, ByteView tag) const {
  std::lock_guard<std::mutex> lk(impl_->mutex);
  auto s = impl_->Use(name, kSlotMac);
  if (!s.HasValue()) return Res<bool>::FromError(s.Error());
  if (s.Value()->proto.algorithm != SlotAlgorithm::kHmacSha256) return Fail<bool>(CryptoErrc::kIncompatibleArguments);
  auto mac = MessageAuthnCode::Create(HashAlgorithm::kSha256, std::get<SecretKey>(s.Value()->key));
  if (!mac.HasValue()) return Res<bool>::FromError(mac.Error());
  return mac.Value().Verify(data, tag);
}

Res<ByteVector> KeyStorageProvider::Sign(std::string const &name, ByteView message) const {
  std::lock_guard<std::mutex> lk(impl_->mutex);
  auto s = impl_->Use(name, kSlotSign);
  if (!s.HasValue()) return Res<ByteVector>::FromError(s.Error());
  if (IsSymmetric(s.Value()->proto.algorithm)) return Fail<ByteVector>(CryptoErrc::kIncompatibleArguments);
  return std::get<PrivateKey>(s.Value()->key).Sign(message);
}

Res<bool> KeyStorageProvider::Verify(std::string const &name, ByteView message, ByteView signature) const {
  std::lock_guard<std::mutex> lk(impl_->mutex);
  auto s = impl_->Use(name, kSlotVerify);
  if (!s.HasValue()) return Res<bool>::FromError(s.Error());
  if (IsSymmetric(s.Value()->proto.algorithm)) return Fail<bool>(CryptoErrc::kIncompatibleArguments);
  auto pub = std::get<PrivateKey>(s.Value()->key).GetPublicKey();
  if (!pub.HasValue()) return Res<bool>::FromError(pub.Error());
  return pub.Value().Verify(message, signature);
}

Res<ByteVector> KeyStorageProvider::GetPublicKeyDer(std::string const &name) const {
  std::lock_guard<std::mutex> lk(impl_->mutex);
  auto it = impl_->slots.find(name);
  if (it == impl_->slots.end()) return Fail<ByteVector>(CryptoErrc::kUnknownIdentifier);
  Slot const &s = it->second;
  if (IsSymmetric(s.proto.algorithm)) return Fail<ByteVector>(CryptoErrc::kIncompatibleArguments);
  if (s.state == SlotState::kCorrupted) return Fail<ByteVector>(CryptoErrc::kAuthTagNotValid);
  if (s.state == SlotState::kEmpty) return Fail<ByteVector>(CryptoErrc::kEmptyContainer);
  auto pub = std::get<PrivateKey>(s.key).GetPublicKey();
  if (!pub.HasValue()) return Res<ByteVector>::FromError(pub.Error());
  return pub.Value().ToDer();
}

} // namespace ara::crypto
