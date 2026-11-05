# ara::crypto - conformance to AUTOSAR AP Cryptography

## Verification basis (read this first)

* Target release: **R25-11**. Its SWS PDF could not be fetched from the build environment (host not allow-listed).
  The R24-11 SWS was reachable but only an excerpt came back, so **no requirement ID and no numeric error value is
  verified or quoted**.
* Confirmed from that excerpt (R24-11): the global factories `LoadCryptoProvider`, `LoadKeyStorageProvider`,
  `LoadX509Provider`; the provider factories `CreateRandomGeneratorCtx`, `CreateKeyDerivationFunctionCtx`,
  `CreateHashFunctionCtx`, `CreateMessageAuthnCodeCtx`, `CreateSymmetricBlockCipherCtx`, `CreateStreamCipherCtx`,
  `CreateAuthCipherCtx`, `CreateSymmetricKeyWrapperCtx`, `CreateVerifierPublicCtx`, `CreateSignerPrivateCtx`,
  `CreateSigEncodePrivateCtx`, `CreateMsgRecoveryPublicCtx`, `CreateEncryptorPublicCtx`,
  `CreateKeyEncapsulatorPublicCtx`; and the error names `kUnknownIdentifier`, `kInvalidArgument`, `kUnsupported`,
  `kInsufficientCapacity`, `kBusyResource`.

## The honest summary

**The crypto layer is a pragmatic, OpenSSL-backed subset, not an implementation of the SWS object model.**
It provides working, tested primitives and a key-slot store. `ara::crypto::cryp` is a thin SWS-*shaped* facade over
those primitives: factory names match, method signatures are simplified. There is no `CryptoObject` hierarchy,
no `ReadOnlyMemRegion`/`WritableMemRegion`, no `CryptoPrimitiveId`, no X.509.

## Primitives (crypto.h) and facade (provider.h)

| Capability | Status | Notes |
|---|---|---|
| `LoadCryptoProvider` | Partial | returns a shared built-in provider (`ap-openssl`) |
| `CreateRandomGeneratorCtx` | Partial | OpenSSL CSPRNG, `Generate(n)`; no seeding/`SetSeed`, no entropy-state API |
| `CreateHashFunctionCtx` | Partial | SHA-256/384/512; Start/Update/Finish; no SHA-3, no `Reset`/`GetDigest` split |
| `CreateMessageAuthnCodeCtx` | Partial | HMAC-SHA-2; key usage `kUsageMac` enforced; context must be re-keyed after Finish; no CMAC/GMAC |
| `CreateSignerPrivateCtx` / `CreateVerifierPublicCtx` | Partial | Ed25519 and ECDSA-P256/SHA-256; algorithm/key mismatch -> `kIncompatibleArguments`; no RSA, no hash-pre-image/context-string API |
| `CreateAuthCipherCtx` | Partial | AES-GCM available as free functions `AeadEncrypt/Decrypt/Seal/Open`, not yet as a context |
| `CreateSymmetricBlockCipherCtx` (AES-CBC/CTR) | Missing | |
| `CreateStreamCipherCtx` | Missing | |
| `CreateKeyDerivationFunctionCtx` (HKDF/PBKDF2) | Missing | |
| `CreateSymmetricKeyWrapperCtx` | Missing | |
| `CreateEncryptorPublicCtx`, `CreateKeyEncapsulatorPublicCtx`, `CreateSigEncodePrivateCtx`, `CreateMsgRecoveryPublicCtx` | Missing | |
| Constant-time compare, secure zeroing | Implemented | `ConstantTimeEquals`, `SecureZero`; keys zeroised on destruction |

## Keys and storage

| Capability | Status | Notes |
|---|---|---|
| `KeyStorageProvider` / `KeySlot` (`LoadKeyStorageProvider`) | Partial | `KeyStorageProvider` with prototypes (algorithm, usage, exportable, persistent), generate/import/export/clear, per-slot usage enforcement, AES-256-GCM persistence via ara::per with tamper/copy detection. Slot API is name-based, not `KeySlot` objects; no transactions (`BeginTransaction/Commit`), no slot-content-change listeners, no `Reserve` |
| `SecretSeed` | Missing | |
| Key usage restrictions | Partial | whitelist bits {encrypt, decrypt, MAC}; the SWS allowed-usage set is larger |
| `CryptoObject`/`KeyMaterial` serialization | Partial | PKCS#8 / SPKI DER for asymmetric keys only; no `ExportPublicly`/`ExportSecurely` object model |
| X.509: `LoadX509Provider`, certificates, CSR, revocation | Missing | |
| Keys bound to Crypto Stack / HSM | Missing | software only |

## Errors

| Item | Status | Notes |
|---|---|---|
| `kUnknownIdentifier`, `kInvalidArgument`, `kUnsupported`, `kInsufficientCapacity`, `kBusyResource` | Names confirmed | `kBusyResource` added in this change |
| Other enumerators (`kUsageViolation`, `kAuthTagNotValid`, ...) | Unverified | names follow the SWS style; **numeric values and domain id are implementation choices** |
| `ThrowAsException` / `ErrorDomain` plumbing | Implemented | |

## Tests

`Testing/crypto`: 44 tests (primitives, key slots with restart and tamper, 7 for the provider facade).
