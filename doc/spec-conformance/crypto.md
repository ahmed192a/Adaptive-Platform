# ara::crypto - conformance to AUTOSAR AP Cryptography

## Verification basis (read this first)

* **Spec sources.** R25-11 first; its PDF could not be fetched. The **R22-11** SWS Cryptography was reachable but only an
  excerpt (up to chapter 7.4.1.1) came back, so chapter 8 (class/method definitions) and the numeric error values are still not
  readable. Everything below that says "confirmed" was read in R22-11/R24-11; scope changes between those releases are minor.
* Target release: **R25-11**. Its SWS PDF could not be fetched from the build environment (host not allow-listed).
  The R24-11 SWS was reachable but only an excerpt came back, so **no requirement ID and no numeric error value is
  verified or quoted**.
* Confirmed from that excerpt (R24-11): the global factories `LoadCryptoProvider`, `LoadKeyStorageProvider`,
  `LoadX509Provider`; the provider factories `CreateRandomGeneratorCtx`, `CreateKeyDerivationFunctionCtx`,
  `CreateHashFunctionCtx`, `CreateMessageAuthnCodeCtx`, `CreateSymmetricBlockCipherCtx`, `CreateStreamCipherCtx`,
  `CreateAuthCipherCtx`, `CreateSymmetricKeyWrapperCtx`, `CreateVerifierPublicCtx`, `CreateSignerPrivateCtx`,
  `CreateSigEncodePrivateCtx`, `CreateMsgRecoveryPublicCtx`, `CreateEncryptorPublicCtx`,
  `CreateKeyEncapsulatorPublicCtx`; and the error names `kUnknownIdentifier`, `kInvalidArgument`, `kUnsupported`,
  `kInsufficientCapacity`, `kBusyResource`; R22-11 adds `kInvalidInputSize`, `kIncompatibleArguments`, `kAlgIdUndefined`.

## The honest summary

**The crypto layer is a pragmatic, OpenSSL-backed subset, not an implementation of the SWS object model.**
It provides working, tested primitives and a key-slot store. `ara::crypto::cryp` is a thin SWS-*shaped* facade over
those primitives: factory names match, method signatures are simplified. There is no `CryptoObject` hierarchy,
no `ReadOnlyMemRegion`/`WritableMemRegion`, no `CryptoPrimitiveId`. "Implemented (shape)" below means the factory name and a working,
tested capability exist; method signatures remain simplified.

## Primitives (crypto.h) and facade (provider.h)

| Capability | Status | Notes |
|---|---|---|
| `LoadCryptoProvider` | Partial | returns a shared built-in provider (`ap-openssl`) |
| `CreateRandomGeneratorCtx` | Partial | OpenSSL CSPRNG, `Generate(n)`; no seeding/`SetSeed`, no entropy-state API |
| `CreateHashFunctionCtx` | Partial | SHA-256/384/512; Start/Update/Finish; no SHA-3, no `Reset`/`GetDigest` split |
| `CreateMessageAuthnCodeCtx` | Partial | HMAC-SHA-2; key usage `kUsageMac` enforced; context must be re-keyed after Finish; no CMAC/GMAC |
| `CreateSignerPrivateCtx` / `CreateVerifierPublicCtx` | Partial | Ed25519 and ECDSA-P256/SHA-256; algorithm/key mismatch -> `kIncompatibleArguments`; no RSA, no hash-pre-image/context-string API |
| `CreateAuthCipherCtx` | Implemented (shape) | `AuthCipherCtx` over AES-GCM (`SetKey`, `Encrypt`/`Decrypt` with nonce + AAD); also free functions `AeadEncrypt/Decrypt/Seal/Open` |
| `CreateSymmetricBlockCipherCtx` (AES-CBC) | Implemented (shape) | `SymmetricBlockCipherCtx`, 128/192/256-bit keys, PKCS#7 padding switch, usage bits enforced; NIST SP 800-38A vector in tests |
| `CreateStreamCipherCtx` (AES-CTR) | Implemented (shape) | `StreamCipherCtx`; NIST SP 800-38A vector in tests |
| `CreateKeyDerivationFunctionCtx` (HKDF/PBKDF2) | Implemented (shape) | `KeyDerivationFunctionCtx` (`KdfKind::kHkdf`/`kPbkdf2`); RFC 5869 and PBKDF2-SHA256 vectors; PBKDF2 enforces OpenSSL minimums by default |
| `CreateSymmetricKeyWrapperCtx` (AES-KW) | Implemented (shape) | RFC 3394 vectors; wrapped key must be exportable; tamper -> `kAuthTagNotValid` |
| `CreateEncryptorPublicCtx` / `CreateDecryptorPrivateCtx` (RSA-OAEP, SHA-256) | Implemented (shape) | `RsaPublicKey`/`RsaPrivateKey` 2048/3072/4096 |
| RSA signatures (RSASSA-PSS, SHA-256) | Implemented (shape) | `RsaSignerPrivateCtx`/`RsaVerifierPublicCtx` (separate from the Ed25519/ECDSA ctxs) |
| `CreateKeyEncapsulatorPublicCtx`, `CreateSigEncodePrivateCtx`, `CreateMsgRecoveryPublicCtx` | Unsupported | factories exist and return `kUnsupported` (no KEM, signature-with-encoding or message-recovery schemes) |
| Constant-time compare, secure zeroing | Implemented | `ConstantTimeEquals`, `SecureZero`; keys zeroised on destruction |

## Keys and storage

| Capability | Status | Notes |
|---|---|---|
| `KeyStorageProvider` / `KeySlot` (`LoadKeyStorageProvider`) | Partial | `KeyStorageProvider` with prototypes (algorithm, usage, exportable, persistent), generate/import/export/clear, per-slot usage enforcement, AES-256-GCM persistence via ara::per with tamper/copy detection. Slot API is name-based, not `KeySlot` objects; no transactions (`BeginTransaction/Commit`), no slot-content-change listeners, no `Reserve` |
| `SecretSeed` | Implemented (shape) | `Generate`/`Import`, `XorWith`, `Jump`/`Next` (big-endian counter), `ToKey`, `DeriveKey` (HKDF); exact SWS method set is Unverified |
| Key usage restrictions | Partial | whitelist bits {encrypt, decrypt, MAC}; the SWS allowed-usage set is larger |
| `CryptoObject`/`KeyMaterial` serialization | Partial | PKCS#8 / SPKI DER for asymmetric keys only; no `ExportPublicly`/`ExportSecurely` object model |
| X.509: `LoadX509Provider`, certificates, chain validation | Implemented (shape) | `X509Certificate` (DER/PEM, names, serial, validity, CA flag, SPKI, fingerprint), `IssueCertificate` (root/intermediate/leaf), `X509Provider` (trust anchors, `VerifyChain` with time, CA/basicConstraints, signatures; failure carries the OpenSSL `X509_V_ERR_*` as support data) |
| X.509: CSR, CRL/OCSP revocation, certificate storage/`CertMgmt` | Missing | |
| `LoadKeyStorageProvider` | Partial | shared *volatile* provider; persistent slots use `KeyStorageProvider::Create` |
| Keys bound to Crypto Stack / HSM | Missing | software only |

## Errors

| Item | Status | Notes |
|---|---|---|
| `kUnknownIdentifier`, `kInvalidArgument`, `kUnsupported`, `kInsufficientCapacity`, `kBusyResource` | Names confirmed | `kBusyResource` added in this change |
| `kAlgIdUndefined` | Name confirmed (R22-11) | added; numeric value unverified |
| Other enumerators (`kUsageViolation`, `kAuthTagNotValid`, ...) | Unverified | names follow the SWS style; **numeric values and domain id are implementation choices** |
| `ThrowAsException` / `ErrorDomain` plumbing | Implemented | |

## Tests

`Testing/crypto`: 73 tests (primitives, key slots with restart and tamper, provider facade, 29 for the extras: HKDF RFC 5869,
PBKDF2, NIST SP 800-38A CBC/CTR, RFC 3394 key wrap, SecretSeed, RSA, X.509 chains incl. expiry/tamper/non-CA).

## Demo application

`App/demo/secure_vault` (`secure_vault demo <dir>`) walks through PBKDF2 -> persistent key slots -> AES-GCM seal/unseal ->
Ed25519 signatures -> restart and wrong-passphrase checks -> RSA-OAEP key transport -> X.509 chain validation, with a readable
trace; it runs as the `secure_vault_demo` ctest.
