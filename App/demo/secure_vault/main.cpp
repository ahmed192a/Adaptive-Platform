/**
 * secure_vault - a small tool built on ara::crypto.
 *
 *   secure_vault demo   <dir>                  run the whole story with a trace (exit code != 0 on any failure)
 *   secure_vault init   <dir> --pass P         create the vault (salt + persistent key slots)
 *   secure_vault seal   <dir> --pass P <in> <out>
 *   secure_vault unseal <dir> --pass P <in> <out>
 *   secure_vault sign   <dir> --pass P <file> <sig>
 *   secure_vault verify <dir> --pass P <file> <sig>
 *   secure_vault pki    <dir>                  issue root -> intermediate -> leaf, validate, then tamper
 *
 * The slot store is wrapped with a key derived from the passphrase (PBKDF2-HMAC-SHA256), so a wrong
 * passphrase makes the slots read as corrupted instead of silently producing garbage.
 */
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ara/core/instance_specifier.h"
#include "ara/crypto/extras.h"
#include "ara/crypto/key_slot.h"
#include "ara/per/key_value_storage.h"
#include "ara/per/persistency.h"

namespace fs = std::filesystem;
using namespace ara::crypto;

namespace {

constexpr char const *kAesSlot = "vault/aes";
constexpr char const *kSignSlot = "vault/signing";
constexpr std::uint32_t kPbkdf2Iterations = 100000;

std::string Hex(ByteVector const &v, std::size_t max = 8) {
  std::ostringstream o;
  for (std::size_t i = 0; i < v.size() && i < max; ++i) o << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(v[i]);
  if (v.size() > max) o << "...";
  return o.str();
}

bool ReadFile(fs::path const &p, ByteVector &out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}
bool WriteFile(fs::path const &p, ByteVector const &data) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<char const *>(data.data()), static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(f);
}

struct Trace {
  int failures{0};
  bool quiet{false}; ///< file commands only report failures
  void Step(std::string const &t) const { if (!quiet) std::cout << "\n== " << t << '\n'; }
  void Info(std::string const &t) const { if (!quiet) std::cout << "   " << t << '\n'; }
  void Check(bool ok, std::string const &what) {
    if (ok && quiet) return;
    std::cout << "   [" << (ok ? " ok " : "FAIL") << "] " << what << '\n';
    if (!ok) ++failures;
  }
};

template <typename R> std::string Err(R const &r) { return r.HasValue() ? "" : std::string(r.Error().Message()); }

/// Opens (or creates) the vault: derives the wrapping key and opens the persistent slot store.
class Vault {
public:
  static std::unique_ptr<Vault> Open(fs::path const &dir, std::string const &pass, std::string &err) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    ByteVector salt;
    if (!ReadFile(dir / "salt.bin", salt)) {
      auto s = GenerateRandom(16);
      if (!s.HasValue() || !WriteFile(dir / "salt.bin", s.Value())) { err = "cannot create salt"; return nullptr; }
      salt = s.Value();
    }
    auto wrap = Pbkdf2(HashAlgorithm::kSha256, ByteView(pass), salt, kPbkdf2Iterations, 32);
    if (!wrap.HasValue()) { err = "kdf: " + Err(wrap); return nullptr; }
    auto cfg = ara::per::PersistencyConfig::FromJson(R"({"storageRoot":")" + (dir / "per").string() +
                                                     R"(","storages":[{"specifier":"Vault/Keys","type":"kvs"}]})");
    if (!cfg.HasValue() || !ara::per::Configure(std::move(cfg).Value()).HasValue()) { err = "persistency config"; return nullptr; }
    auto kvs = ara::per::OpenKeyValueStorage(ara::core::InstanceSpecifier("Vault/Keys"));
    if (!kvs.HasValue()) { err = "open storage"; return nullptr; }
    auto prov = KeyStorageProvider::Create(kvs.Value(), wrap.Value());
    SecureZero(wrap.Value().data(), wrap.Value().size());
    if (!prov.HasValue()) { err = "key storage: " + Err(prov); return nullptr; }
    auto v = std::unique_ptr<Vault>(new Vault(std::move(prov).Value()));
    KeySlotPrototype aes{kAesSlot, SlotAlgorithm::kAes256Gcm, kSlotEncrypt | kSlotDecrypt, false, true};
    KeySlotPrototype sig{kSignSlot, SlotAlgorithm::kEd25519, kSlotSign | kSlotVerify, false, true};
    if (!v->keys.DefineSlot(aes).HasValue() || !v->keys.DefineSlot(sig).HasValue()) { err = "define slots"; return nullptr; }
    return v;
  }
  KeyStorageProvider keys;

private:
  explicit Vault(KeyStorageProvider k) : keys(std::move(k)) {}
};

bool EnsureKeys(Vault &v, Trace &t) {
  for (char const *name : {kAesSlot, kSignSlot}) {
    auto info = v.keys.GetSlotInfo(name);
    if (!info.HasValue()) return false;
    if (info.Value().state == SlotState::kCorrupted) {
      t.Check(false, std::string("slot ") + name + " is corrupted (wrong passphrase or tampered store)");
      return false;
    }
    if (info.Value().state == SlotState::kEmpty) {
      auto g = v.keys.Generate(name);
      t.Check(g.HasValue(), std::string("generated key in slot ") + name);
      if (!g.HasValue()) return false;
    } else {
      t.Info(std::string("slot ") + name + " already holds a key");
    }
  }
  return true;
}

// ------------------------------------------------------------------ commands
int RunPki(fs::path const &dir, Trace &t) {
  t.Step("X.509 chain: root (Ed25519) -> intermediate (ECDSA P-256) -> leaf");
  auto rootKey = PrivateKey::Generate(SignatureAlgorithm::kEd25519).Value();
  auto caKey = PrivateKey::Generate(SignatureAlgorithm::kEcdsaP256Sha256).Value();
  auto leafKey = PrivateKey::Generate(SignatureAlgorithm::kEcdsaP256Sha256).Value();
  auto root = IssueCertificate({"Vault Root CA", rootKey.GetPublicKey().Value().ToDer().Value(), 1, 0, 0, true}, nullptr, rootKey);
  auto ca = root.HasValue() ? IssueCertificate({"Vault Intermediate CA", caKey.GetPublicKey().Value().ToDer().Value(), 2, 0, 0, true},
                                               &root.Value(), rootKey)
                            : root;
  auto leaf = ca.HasValue() ? IssueCertificate({"ecu-1.vault", leafKey.GetPublicKey().Value().ToDer().Value(), 3, 0, 0, false},
                                               &ca.Value(), caKey)
                            : ca;
  t.Check(root.HasValue() && ca.HasValue() && leaf.HasValue(), "issued three certificates");
  if (!leaf.HasValue()) return 1;
  for (auto const *c : {&root.Value(), &ca.Value(), &leaf.Value()})
    t.Info(c->SubjectName() + "  <-  " + c->IssuerName() + "  serial=" + c->SerialNumberHex() + "  CA=" + (c->IsCa() ? "yes" : "no") +
           "  sha256=" + Hex(c->Fingerprint().Value()));
  if (auto pem = leaf.Value().ToPem(); pem.HasValue()) {
    std::ofstream(dir / "leaf.pem") << pem.Value();
    t.Info("wrote " + (dir / "leaf.pem").string());
  }

  X509Provider provider;
  t.Check(provider.AddTrustAnchor(root.Value()).HasValue(), "root added as trust anchor");
  t.Check(provider.VerifyChain(leaf.Value(), {ca.Value()}).HasValue(), "chain leaf -> intermediate -> root validates");
  auto missing = provider.VerifyChain(leaf.Value());
  t.Check(!missing.HasValue(), "chain without the intermediate is rejected (x509 error " +
                                   std::to_string(missing.HasValue() ? 0 : missing.Error().SupportData()) + ")");
  std::time_t const future = leaf.Value().NotAfter() + 86400;
  t.Check(!provider.VerifyChain(leaf.Value(), {ca.Value()}, future).HasValue(), "an expired leaf is rejected");
  auto der = leaf.Value().ToDer().Value();
  der[der.size() - 4] ^= 0x01;
  auto tampered = X509Certificate::FromDer(der);
  t.Check(!tampered.HasValue() || !provider.VerifyChain(tampered.Value(), {ca.Value()}).HasValue(),
          "a leaf with a flipped signature bit is rejected");
  return 0;
}

int RunDemo(fs::path const &dir, Trace &t) {
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  std::string const pass = "correct horse battery staple";
  std::string err;

  t.Step("1. create the vault: passphrase -> PBKDF2 -> wrapping key -> persistent key slots");
  auto vault = Vault::Open(dir, pass, err);
  t.Check(vault != nullptr, "vault opened " + err);
  if (!vault) return 1;
  if (!EnsureKeys(*vault, t)) return 1;
  for (auto const &s : vault->keys.ListSlots())
    t.Info("slot " + s.prototype.name + (s.prototype.persistent ? " (persistent)" : "") + (s.prototype.exportable ? " exportable" : " non-exportable"));

  t.Step("2. seal a file with AES-256-GCM (key never leaves the slot)");
  std::string const message = "Firmware 2.4.1 release notes: brake controller timing fix.";
  auto sealed = vault->keys.Seal(kAesSlot, ByteView(std::string("doc-v1")), ByteView(message));
  t.Check(sealed.HasValue(), "sealed " + std::to_string(message.size()) + " bytes -> " + std::to_string(sealed.HasValue() ? sealed.Value().size() : 0) + " bytes (nonce+ciphertext+tag)");
  if (!sealed.HasValue()) return 1;
  (void)WriteFile(dir / "notes.sealed", sealed.Value());
  auto opened = vault->keys.Open(kAesSlot, ByteView(std::string("doc-v1")), sealed.Value());
  t.Check(opened.HasValue() && std::string(opened.Value().begin(), opened.Value().end()) == message, "unsealed and compared with the original");
  auto wrongAad = vault->keys.Open(kAesSlot, ByteView(std::string("doc-v2")), sealed.Value());
  t.Check(!wrongAad.HasValue(), "wrong associated data is rejected");
  auto flipped = sealed.Value();
  flipped[20] ^= 0x80;
  t.Check(!vault->keys.Open(kAesSlot, ByteView(std::string("doc-v1")), flipped).HasValue(), "a flipped ciphertext bit is rejected");

  t.Step("3. sign and verify with Ed25519");
  auto sig = vault->keys.Sign(kSignSlot, ByteView(message));
  t.Check(sig.HasValue(), "signature " + Hex(sig.HasValue() ? sig.Value() : ByteVector{}, 12));
  if (!sig.HasValue()) return 1;
  t.Check(vault->keys.Verify(kSignSlot, ByteView(message), sig.Value()).ValueOr(false), "signature verifies");
  t.Check(!vault->keys.Verify(kSignSlot, ByteView(message + "!"), sig.Value()).ValueOr(true), "signature over a modified message does not verify");

  t.Step("4. restart: keys survive in the wrapped store");
  auto pubBefore = vault->keys.GetPublicKeyDer(kSignSlot).Value();
  vault.reset();
  vault = Vault::Open(dir, pass, err);
  t.Check(vault != nullptr, "vault reopened with the same passphrase");
  if (!vault) return 1;
  t.Check(vault->keys.GetPublicKeyDer(kSignSlot).ValueOr(ByteVector{}) == pubBefore, "same signing key after restart");
  auto again = vault->keys.Open(kAesSlot, ByteView(std::string("doc-v1")), sealed.Value());
  t.Check(again.HasValue(), "file sealed before the restart still opens");
  vault.reset();
  auto wrong = Vault::Open(dir, "wrong passphrase", err);
  bool rejected = wrong == nullptr;
  if (wrong) {
    auto info = wrong->keys.GetSlotInfo(kAesSlot);
    rejected = info.HasValue() && info.Value().state == SlotState::kCorrupted;
  }
  t.Check(rejected, "a wrong passphrase cannot read the slots");

  t.Step("5. RSA-OAEP wraps a fresh AES key for another party");
  auto rsa = RsaPrivateKey::Generate(2048).Value();
  auto session = SecretKey::Generate(32, kUsageEncrypt | kUsageDecrypt, true).Value();
  auto wrapped = rsa.GetPublicKey().Value().EncryptOaep(session.Export().Value());
  t.Check(wrapped.HasValue(), "session key wrapped for the recipient (" + std::to_string(wrapped.HasValue() ? wrapped.Value().size() : 0) + " bytes)");
  auto unwrapped = rsa.DecryptOaep(wrapped.Value());
  t.Check(unwrapped.HasValue() && unwrapped.Value() == session.Export().Value(), "recipient recovers the same session key");

  int rc = RunPki(dir, t);
  if (rc != 0) return rc;

  std::cout << "\n" << (t.failures == 0 ? "secure_vault demo: ALL CHECKS PASSED" : "secure_vault demo: " + std::to_string(t.failures) + " CHECK(S) FAILED")
            << std::endl;
  return t.failures == 0 ? 0 : 1;
}

struct Args {
  std::string cmd, dir, pass;
  std::vector<std::string> pos;
};
Args Parse(int argc, char **argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    std::string s = argv[i];
    if (s == "--pass" && i + 1 < argc) a.pass = argv[++i];
    else if (a.cmd.empty()) a.cmd = s;
    else if (a.dir.empty()) a.dir = s;
    else a.pos.push_back(s);
  }
  return a;
}

int Usage() {
  std::cerr << "usage: secure_vault demo <dir> | pki <dir> | init|seal|unseal|sign|verify <dir> --pass P [files]\n";
  return 2;
}

} // namespace

int main(int argc, char **argv) {
  Args a = Parse(argc, argv);
  if (a.cmd.empty() || a.dir.empty()) return Usage();
  Trace t;
  fs::path const dir = a.dir;
  if (a.cmd == "demo") return RunDemo(dir, t);
  if (a.cmd == "pki") { std::error_code ec; fs::create_directories(dir, ec); int rc = RunPki(dir, t); return rc != 0 || t.failures != 0 ? 1 : 0; }
  if (a.pass.empty()) { std::cerr << "--pass is required\n"; return 2; }
  t.quiet = true;
  std::string err;
  auto vault = Vault::Open(dir, a.pass, err);
  if (!vault) { std::cerr << "error: " << err << '\n'; return 1; }
  if (!EnsureKeys(*vault, t)) return 1;
  if (a.cmd == "init") return 0;
  if (a.pos.size() != 2) return Usage();
  ByteVector in;
  if (!ReadFile(a.pos[0], in)) { std::cerr << "cannot read " << a.pos[0] << '\n'; return 1; }
  if (a.cmd == "seal" || a.cmd == "unseal") {
    auto r = a.cmd == "seal" ? vault->keys.Seal(kAesSlot, ByteView(std::string("vault")), in) : vault->keys.Open(kAesSlot, ByteView(std::string("vault")), in);
    if (!r.HasValue()) { std::cerr << a.cmd << " failed: " << Err(r) << '\n'; return 1; }
    return WriteFile(a.pos[1], r.Value()) ? 0 : 1;
  }
  if (a.cmd == "sign") {
    auto r = vault->keys.Sign(kSignSlot, in);
    if (!r.HasValue()) { std::cerr << "sign failed: " << Err(r) << '\n'; return 1; }
    return WriteFile(a.pos[1], r.Value()) ? 0 : 1;
  }
  if (a.cmd == "verify") {
    ByteVector sig;
    if (!ReadFile(a.pos[1], sig)) { std::cerr << "cannot read " << a.pos[1] << '\n'; return 1; }
    auto r = vault->keys.Verify(kSignSlot, in, sig);
    bool ok = r.HasValue() && r.Value();
    std::cout << (ok ? "signature OK" : "signature INVALID") << std::endl;
    return ok ? 0 : 1;
  }
  return Usage();
}
