#ifndef TESTING_PER_FIXTURE_H_
#define TESTING_PER_FIXTURE_H_

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include "ara/per/persistency.h"

namespace pertest {

namespace fs = std::filesystem;

/// Creates a unique storage root, writes a config with one kvs and one file
/// storage and installs it.  Extra JSON storages can be appended by tests.
class PerFixture : public ::testing::Test {
protected:
  void SetUp() override {
    std::random_device rd;
    root = fs::temp_directory_path() / ("ara_per_ut_" + std::to_string(rd()));
    fs::create_directories(root);
    Install(DefaultStorages());
  }
  void TearDown() override {
    // release registry state before removing the files
    (void)ara::per::Configure(*ara::per::PersistencyConfig::FromJson(EmptyConfig()).Ok());
    std::error_code ec;
    fs::remove_all(root, ec);
  }

  std::string EmptyConfig() const {
    return R"({"storageRoot":")" + (root / "unused").string() + R"("})";
  }
  virtual std::string DefaultStorages() const {
    return R"([
      {"specifier":"App/Kvs","type":"kvs","initialValues":{"volume":5,"name":"default"}},
      {"specifier":"App/Files","type":"file","maxFiles":3,"maxSize":64}
    ])";
  }
  void Install(std::string const &storagesJson) {
    std::string const doc = R"({"storageRoot":")" + root.string() + R"(","storages":)" + storagesJson + "}";
    auto cfg = ara::per::PersistencyConfig::FromJson(doc);
    ASSERT_TRUE(cfg.HasValue()) << cfg.Error().Message();
    auto res = ara::per::Configure(std::move(cfg).Value());
    ASSERT_TRUE(res.HasValue()) << res.Error().Message();
  }

  static ara::core::InstanceSpecifier Spec(char const *s) { return ara::core::InstanceSpecifier(s); }
  fs::path KvsFile() const { return root / "App.Kvs" / "kvs.json"; }

  fs::path root;
};

} // namespace pertest

#endif
