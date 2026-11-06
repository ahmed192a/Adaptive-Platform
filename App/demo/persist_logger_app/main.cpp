// persist_logger_app: a tiny "vehicle data logger" that shows ara::log and ara::per working together.
//   * boot counter and settings live in a key-value storage (survive restarts)
//   * every run appends a trip record to a file storage, which keeps a CRC-protected redundancy copy
//   * --corrupt damages the trip file on purpose and RecoverAllFiles() restores it
// Usage: persist_logger_app [--dir <path>] [--corrupt]
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "ara/core/byte.h"
#include "ara/core/initialization.h"
#include "ara/core/span.h"
#include "ara/log/logging.h"
#include "ara/per/persistency.h"

namespace fs = std::filesystem;

int main(int argc, char **argv) {
  fs::path dir = fs::temp_directory_path() / "persist_logger_app";
  bool corrupt = false;
  for (int i = 1; i < argc; ++i) {
    std::string const a = argv[i];
    if (a == "--dir" && i + 1 < argc) dir = argv[++i];
    else if (a == "--corrupt") corrupt = true;
  }
  fs::create_directories(dir);

  (void)ara::core::Initialize();
  ara::log::InitLogging("PLOG", "persist logger demo", ara::log::LogLevel::kInfo, ara::log::LogMode::kConsole);
  auto &log = ara::log::CreateLogger("MAIN", "application", ara::log::LogLevel::kVerbose);
  auto &perLog = ara::log::CreateLogger("PERS", "persistency", ara::log::LogLevel::kInfo);

  std::string const cfgJson = R"({"storageRoot":")" + dir.string() +
      R"(","storages":[{"specifier":"Logger/Settings","type":"kvs","initialValues":{"boots":0,"unit":"km"}},)"
      R"({"specifier":"Logger/Trips","type":"file","maxFiles":8,"maxSize":4096}]})";
  auto cfg = ara::per::PersistencyConfig::FromJson(cfgJson);
  if (!cfg.HasValue() || !ara::per::Configure(std::move(cfg).Value()).HasValue()) {
    log.LogError() << "persistency configuration failed";
    return 1;
  }

  // --- key-value storage: boot counter ------------------------------------------------
  auto kvs = ara::per::OpenKeyValueStorage(ara::core::InstanceSpecifier("Logger/Settings")).Value();
  std::int64_t boots = kvs->GetValue<std::int64_t>("boots").ValueOr(0) + 1;
  (void)kvs->SetValue("boots", boots);
  (void)kvs->SyncToStorage();
  perLog.LogInfo() << "boot #" << boots << " (unit=" << kvs->GetValue<std::string>("unit").ValueOr("?") << ")";
  log.LogVerbose() << "verbose messages are visible because this logger is at kVerbose";
  log.LogDebug() << "debug: dir=" << dir.string();

  // --- file storage: trip records with redundancy ----------------------------------------
  auto trips = ara::per::OpenFileStorage(ara::core::InstanceSpecifier("Logger/Trips")).Value();
  {
    auto w = trips->OpenFileReadWrite("trips.log", ara::per::OpenMode::kAppend).Value();
    (void)w->WriteText("trip " + std::to_string(boots) + ": 12.5 km\n");
    (void)w->SyncToFile(); // also refreshes the redundancy copy
  }
  std::array<ara::core::Byte, 4> const header{ara::core::Byte{0xDE}, ara::core::Byte{0xAD}, ara::core::Byte{0xBE}, ara::core::Byte{0xEF}};
  log.LogInfo() << "record header: " << ara::core::Span<const ara::core::Byte>(header);

  if (corrupt) {
    trips.reset();
    std::ofstream(dir / "Logger.Trips" / "files" / "trips.log", std::ios::trunc) << "###bit-rot###";
    log.LogWarn() << "trip file corrupted on purpose";
    auto rec = ara::per::RecoverAllFiles(ara::core::InstanceSpecifier("Logger/Trips"));
    if (rec.HasValue()) log.LogInfo() << "RecoverAllFiles restored the trip file from its redundancy copy";
    else log.LogError() << "recovery failed: " << rec.Error();
    trips = ara::per::OpenFileStorage(ara::core::InstanceSpecifier("Logger/Trips")).Value();
  }
  auto r = trips->OpenFileReadOnly("trips.log").Value();
  std::cout << "---- trips.log ----\n" << r->ReadText().ValueOr("") << "-------------------\n";
  (void)ara::core::Deinitialize();
  return 0;
}
