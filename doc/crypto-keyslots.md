# Crypto key slots

`ara::crypto::KeyStorageProvider` (`src/ara/crypto/key_slot.h`) is a SWS-style *KeySlot* facade over the
OpenSSL primitives. A slot is declared with a prototype and holds at most one key.

```cpp
auto kvs = ara::per::OpenKeyValueStorage(ara::core::InstanceSpecifier("Sec/Keys")).Value();
auto prov = KeyStorageProvider::Create(kvs, wrappingKey32).Value();   // or CreateVolatile()

prov.DefineSlot({"fw.sign", SlotAlgorithm::kEd25519, kSlotSign, /*exportable*/ false, /*persistent*/ true});
prov.Generate("fw.sign");                         // or Import(name, raw | PKCS#8 DER)
auto sig = prov.Sign("fw.sign", message).Value();
auto pub = prov.GetPublicKeyDer("fw.sign").Value();   // public part is always available
```

| Algorithm | Allowed usage | Operations |
|---|---|---|
| `kAes256Gcm` | encrypt, decrypt | `Seal`, `Open` |
| `kHmacSha256` | mac | `ComputeMac`, `VerifyMac` |
| `kEd25519`, `kEcdsaP256` | sign, verify | `Sign`, `Verify`, `GetPublicKeyDer` |

* **Usage is enforced** per slot (`kUsageViolation`); a prototype that asks for a usage its algorithm cannot
  provide is rejected. Only symmetric slots can be `exportable`.
* **Persistence:** persistent slots are stored in the given `KeyValueStorage` under `keyslot/<name>` as
  `nonce || AES-256-GCM(wrappingKey)(material) || tag` (hex). The AAD binds slot name, algorithm, usage and
  exportable flag, so editing the blob, copying it to another slot, or widening the policy makes the slot
  `kCorrupted` on the next `DefineSlot`. Corrupted slots refuse every operation (`kAuthTagNotValid`) until
  `Generate`/`Import`/`Clear` repairs them.
* The wrapping key is supplied by the caller (on a target: derive/unseal it from an HSM/TPM or secure boot
  chain). It is never stored.
* A volatile provider (`CreateVolatile`) accepts only non-persistent slots.
