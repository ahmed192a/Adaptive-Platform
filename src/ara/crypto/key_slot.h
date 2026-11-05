/**
 * @file key_slot.h
 * @brief ara::crypto key-slot facade: KeySlotPrototype / KeyStorageProvider.
 *
 * Modelled on the SWS Cryptography KeySlot concept.  A slot is declared with a
 * prototype (algorithm, allowed usage, exportable, persistent) and then holds at
 * most one key.  Key material never leaves the provider except through Export()
 * on an exportable slot; all cryptographic operations are performed by name.
 *
 *  - Usage is enforced per slot: a sign-only key cannot verify, a MAC-only key
 *    cannot encrypt, and so on (kUsageViolation).
 *  - Persistent slots are stored through ara::per::KeyValueStorage as
 *    AES-256-GCM(wrappingKey) blobs.  The slot name, algorithm, usage and
 *    exportable flag are bound as AAD, so a blob that is edited, copied into
 *    another slot, or loaded under a different prototype fails authentication
 *    and the slot is reported as kCorrupted (never silently used).
 *  - A volatile provider (no storage) only accepts non-persistent slots.
 *
 * The wrapping key should come from a hardware/secure source on a real ECU;
 * here the caller supplies it.  All methods are thread safe and exception free.
 */
#ifndef ARA_CRYPTO_KEY_SLOT_H_
#define ARA_CRYPTO_KEY_SLOT_H_

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

#include "ara/crypto/crypto.h"
#include "ara/per/key_value_storage.h"

namespace ara::crypto {

enum class SlotAlgorithm : std::uint8_t {
  kAes256Gcm = 1,   ///< 32-byte symmetric key, AEAD
  kHmacSha256 = 2,  ///< >= 16-byte symmetric key, MAC
  kEd25519 = 3,
  kEcdsaP256 = 4,
};

enum SlotUsage : std::uint32_t {
  kSlotEncrypt = 1U << 0,
  kSlotDecrypt = 1U << 1,
  kSlotMac = 1U << 2,
  kSlotSign = 1U << 3,
  kSlotVerify = 1U << 4,
};

struct KeySlotPrototype {
  std::string name;                      ///< unique, [A-Za-z0-9_./-], <= 64 chars
  SlotAlgorithm algorithm{SlotAlgorithm::kAes256Gcm};
  std::uint32_t usage{0};                ///< SlotUsage bits valid for the algorithm
  bool exportable{false};
  bool persistent{false};
};

enum class SlotState : std::uint8_t { kEmpty, kOccupied, kCorrupted };

struct KeySlotInfo {
  KeySlotPrototype prototype;
  SlotState state{SlotState::kEmpty};
};

class KeyStorageProvider {
public:
  /// Volatile provider: only non-persistent slots.
  static KeyStorageProvider CreateVolatile();
  /// Persistent provider.  `wrappingKey` must be 32 bytes.
  static Res<KeyStorageProvider> Create(ara::per::SharedHandle<ara::per::KeyValueStorage> storage,
                                        ByteView wrappingKey);

  KeyStorageProvider(KeyStorageProvider &&) noexcept;
  KeyStorageProvider &operator=(KeyStorageProvider &&) noexcept;
  ~KeyStorageProvider();

  /// Declares a slot.  For a persistent slot an existing stored key is loaded and
  /// authenticated; if that fails the slot is still defined but kCorrupted.
  Res<void> DefineSlot(KeySlotPrototype const &proto);
  Res<KeySlotInfo> GetSlotInfo(std::string const &name) const;
  std::vector<KeySlotInfo> ListSlots() const;

  /// Generates a fresh key into the slot (replaces any content, also repairs kCorrupted).
  Res<void> Generate(std::string const &name);
  /// Imports raw bytes (symmetric) or PKCS#8 DER (asymmetric).
  Res<void> Import(std::string const &name, ByteView material);
  /// kKeyNotExportable unless the prototype is exportable.
  Res<ByteVector> Export(std::string const &name) const;
  /// Erases the key (memory and storage).  Idempotent for an empty slot.
  Res<void> Clear(std::string const &name);

  // Operations, each requires the matching usage bit and algorithm.
  Res<ByteVector> Seal(std::string const &name, ByteView aad, ByteView plaintext) const;
  Res<ByteVector> Open(std::string const &name, ByteView aad, ByteView sealed) const;
  Res<ByteVector> ComputeMac(std::string const &name, ByteView data) const;
  Res<bool> VerifyMac(std::string const &name, ByteView data, ByteView tag) const;
  Res<ByteVector> Sign(std::string const &name, ByteView message) const;
  Res<bool> Verify(std::string const &name, ByteView message, ByteView signature) const;
  /// SubjectPublicKeyInfo DER of an asymmetric slot (always allowed, it is public).
  Res<ByteVector> GetPublicKeyDer(std::string const &name) const;

private:
  struct Slot;
  struct Impl;
  explicit KeyStorageProvider(std::unique_ptr<Impl> i);
  std::unique_ptr<Impl> impl_;
};

} // namespace ara::crypto

#endif // ARA_CRYPTO_KEY_SLOT_H_
