/**
 * ap_per_tool - inspect and maintain ara::per storages from the command line.
 *
 *   ap_per_tool -c per.json list
 *   ap_per_tool -c per.json dump App/Kvs
 *   ap_per_tool -c per.json get  App/Kvs volume
 *   ap_per_tool -c per.json set  App/Kvs volume 7        (value is parsed as JSON)
 *   ap_per_tool -c per.json files App/Files
 *   ap_per_tool -c per.json verify                       (exit code 1 if corrupted)
 *   ap_per_tool -c per.json reset App/Kvs
 */
#include <CLI/CLI.hpp>
#include <iostream>

#include "ara/per/persistency.h"

namespace {
using ara::core::InstanceSpecifier;

int Fail(ara::core::ErrorCode const &ec) {
  std::cerr << "error: " << ec.Domain().Name() << ": " << ec.Message() << "\n";
  return 1;
}

bool MakeSpec(std::string const &s, InstanceSpecifier *out) {
  auto spec = InstanceSpecifier::Create(s);
  if (!spec.HasValue()) {
    std::cerr << "error: '" << s << "' is not a valid instance specifier\n";
    return false;
  }
  *out = spec.Value();
  return true;
}
} // namespace

int main(int argc, char **argv) {
  CLI::App app{"AUTOSAR Adaptive persistency maintenance tool"};
  app.require_subcommand(1);
  std::string configPath;
  app.add_option("-c,--config", configPath, "persistency JSON configuration")->required()->check(CLI::ExistingFile);

  auto *list = app.add_subcommand("list", "list configured storages");
  auto *dump = app.add_subcommand("dump", "print a key-value storage as JSON");
  auto *get = app.add_subcommand("get", "print one value");
  auto *set = app.add_subcommand("set", "set one value and sync");
  auto *files = app.add_subcommand("files", "list files of a file storage");
  auto *verify = app.add_subcommand("verify", "open every storage and check integrity");
  auto *reset = app.add_subcommand("reset", "reset a storage to its initial state");

  std::string spec, key, value;
  dump->add_option("specifier", spec)->required();
  get->add_option("specifier", spec)->required();
  get->add_option("key", key)->required();
  set->add_option("specifier", spec)->required();
  set->add_option("key", key)->required();
  set->add_option("value", value)->required();
  files->add_option("specifier", spec)->required();
  reset->add_option("specifier", spec)->required();

  CLI11_PARSE(app, argc, argv);

  auto cfg = ara::per::PersistencyConfig::FromFile(configPath);
  if (!cfg.HasValue()) {
    return Fail(cfg.Error());
  }
  auto const storages = cfg.Value().Storages();
  auto configured = ara::per::Configure(std::move(cfg).Value());
  if (!configured.HasValue()) {
    return Fail(configured.Error());
  }

  if (list->parsed()) {
    for (auto const &[k, s] : storages) {
      std::cout << (s.kind == ara::per::StorageConfig::Kind::kKeyValue ? "kvs  " : "file ")
                << s.specifier << "  " << s.directory.string() << (s.readOnly ? "  [read-only]" : "") << "\n";
    }
    return 0;
  }
  if (verify->parsed()) {
    int bad = 0;
    for (auto const &[k, s] : storages) {
      InstanceSpecifier is(s.specifier);
      ara::core::ErrorCode const ok = ara::per::MakeErrorCode(ara::per::PerErrc::kStorageNotFound);
      (void)ok;
      bool good = false;
      ara::core::ErrorCode err = ok;
      if (s.kind == ara::per::StorageConfig::Kind::kKeyValue) {
        auto r = ara::per::OpenKeyValueStorage(is);
        good = r.HasValue();
        if (!good) err = r.Error();
      } else {
        auto r = ara::per::OpenFileStorage(is);
        good = r.HasValue();
        if (!good) err = r.Error();
      }
      std::cout << (good ? "OK       " : "CORRUPT  ") << s.specifier;
      if (!good) {
        std::cout << "  (" << err.Message() << ")";
        ++bad;
      }
      std::cout << "\n";
    }
    return bad == 0 ? 0 : 1;
  }

  InstanceSpecifier is("tool/placeholder");
  if (!MakeSpec(spec, &is)) {
    return 2;
  }
  if (reset->parsed()) {
    auto k = ara::per::ResetKeyValueStorage(is);
    if (k.HasValue()) return 0;
    auto f = ara::per::ResetAllFiles(is);
    return f.HasValue() ? 0 : Fail(f.Error());
  }
  if (files->parsed()) {
    auto st = ara::per::OpenFileStorage(is);
    if (!st.HasValue()) return Fail(st.Error());
    auto const names = st.Value()->GetAllFileNames();
    for (auto const &n : names.Value()) {
      std::cout << n << "  " << st.Value()->GetCurrentFileSize(n).ValueOr(0) << " bytes\n";
    }
    return 0;
  }

  auto kvs = ara::per::OpenKeyValueStorage(is);
  if (!kvs.HasValue()) {
    return Fail(kvs.Error());
  }
  if (dump->parsed()) {
    nlohmann::json out = nlohmann::json::object();
    auto const keys = kvs.Value()->GetAllKeys();
    for (auto const &k : keys.Value()) {
      out[k] = kvs.Value()->GetValue<nlohmann::json>(k).Value();
    }
    std::cout << out.dump(2) << "\n";
  } else if (get->parsed()) {
    auto v = kvs.Value()->GetValue<nlohmann::json>(key);
    if (!v.HasValue()) return Fail(v.Error());
    std::cout << v.Value().dump() << "\n";
  } else if (set->parsed()) {
    auto parsed = nlohmann::json::parse(value, nullptr, false);
    if (parsed.is_discarded()) {
      parsed = value; // plain text -> JSON string
    }
    auto r = kvs.Value()->SetValue(key, parsed);
    if (!r.HasValue()) return Fail(r.Error());
    auto s = kvs.Value()->SyncToStorage();
    if (!s.HasValue()) return Fail(s.Error());
  }
  return 0;
}
