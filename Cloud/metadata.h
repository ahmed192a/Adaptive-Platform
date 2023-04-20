#ifndef META_DATA_HPP
#define META_DATA_HPP
#include "nlohmann/json.hpp"
#include <iostream>
#include <string>
#include <vector>

class MetaData {
  // MetaData variables
  std::string platformName;
  std::string appName;
  std::string appID;
  unsigned long sizeInBytes;
  std::string version;

public:
  // Constructor Overloading to support different types of initializations
  MetaData() = default;
  MetaData(std::string const &platformName, std::string const &appName,
           std::string const &appID, unsigned long sizeInBytes,
           std::string const &ver)
      : platformName(platformName), appName(appName), appID(appID),
        sizeInBytes(sizeInBytes), version(ver) {}

  explicit MetaData(std::string const &jsonStr) {
    std::cout << "jsonStr: " << jsonStr << std::endl;
    // parse the json string and get the values
    nlohmann::json j = nlohmann::json::parse(jsonStr);
    // if empty json string return
    if (j.empty()) {
      return;
    }
    // {"platformName": "classic", "appID" : "K234" , "appName": "app1", "sizeInBytes": "145", "version": "0.1.0","result":"ok"}
    // parse the sizeinbytes to int

    this->platformName = j["platformName"];
    this->appName = j["appName"];
    this->appID = j["appID"];
    // this->sizeInBytes = j["sizeInBytes"].get<unsigned long>();
    // check if the sizeInBytes is a number or string
    if (j["sizeInBytes"].is_number()) {
      this->sizeInBytes = j["sizeInBytes"].get<unsigned long>();
    } else {
      this->sizeInBytes = std::stoul(j["sizeInBytes"].get<std::string>());
    }
    this->version = j["version"];
  }

  /// operator overloading for ">" sign compare the version of the app
  bool operator>(MetaData const &Meta2) const {

    // split the version string into three parts 1.major 2.minor 3.patch and
    // compare them
    int major1 = atoi(this->version.substr(0, this->version.find(".")).c_str());
    int minor1 =
        atoi(this->version.substr(this->version.find(".") + 1, this->version.find(".")).c_str());
    int patch1 =
        atoi(this->version.substr(this->version.find(".") + 1, this->version.find(".")).c_str());
    int major2 = atoi(Meta2.version.substr(0, Meta2.version.find(".")).c_str());
    int minor2 =
        atoi(Meta2.version.substr(Meta2.version.find(".") + 1, Meta2.version.find(".")).c_str());
    int patch2 =
        atoi(Meta2.version.substr(Meta2.version.find(".") + 1, Meta2.version.find(".")).c_str());
    if (major1 > major2)
      return true;
    else if (major1 == major2) {
      if (minor1 > minor2)
        return true;
      else if (minor1 == minor2) {
        if (patch1 > patch2)
          return true;
        else
          return false;
      } else
        return false;
    } else
      return false;
  }

  /// operator overloading for "<" sign compare the version of the app
  bool operator<(MetaData const &Meta2) const {
    // split the version string into three parts 1.major 2.minor 3.patch and
    // compare them
    int major1 = atoi(this->version.substr(0, this->version.find(".")).c_str());
    int minor1 =
        atoi(this->version.substr(this->version.find(".") + 1, this->version.find(".")).c_str());
    int patch1 =
        atoi(this->version.substr(this->version.find(".") + 1, this->version.find(".")).c_str());
    int major2 = atoi(Meta2.version.substr(0, Meta2.version.find(".")).c_str());
    int minor2 =
        atoi(Meta2.version.substr(Meta2.version.find(".") + 1, Meta2.version.find(".")).c_str());
    int patch2 =
        atoi(Meta2.version.substr(Meta2.version.find(".") + 1, Meta2.version.find(".")).c_str());

    if (major1 < major2)
      return true;
    else if (major1 == major2) {
      if (minor1 < minor2)
        return true;
      else if (minor1 == minor2) {
        if (patch1 < patch2)
          return true;
        else
          return false;
      } else
        return false;
    } else
      return false;
  }

  /// operator overloading for "==" sign to compare the apppName, platformName &
  /// appID only
  bool operator==(MetaData const &Meta2) const {
    // comparing the equivalence of the platformName, appName & appId only to
    // check it is the same app
    if ((this->platformName == Meta2.platformName) &&
        (this->appName == Meta2.appName) && (this->appID == Meta2.appID)) {
      return true;
    } else {
      return false;
    }
  }

  // setters and getters for the class members
  void set_platformName(std::string const &platform_name) {
    this->platformName = platform_name;
  }
  std::string get_platformName(void) const { return this->platformName; }

  void set_appName(std::string const &app_name) { this->appName = app_name; }
  std::string get_appName(void) const { return this->appName; }

  void set_appID(std::string const &appid) { this->appID = appid; }
  std::string get_appID(void) const { return this->appID; }

  void set_sizeInBytes(unsigned long size) { this->sizeInBytes = size; }
  unsigned long get_sizeInBytes(void) const { return this->sizeInBytes; }

  void set_version(std::string const &newversion) { this->version = newversion; }
  std::string get_version() const { return this->version; }

  friend std::istream &operator>>(std::istream &in, MetaData &c) {
    in >> c.platformName >> c.appName >> c.appID >> c.sizeInBytes >> c.version;
    return in;
  }
  friend std::ostream &operator<<(std::ostream &out, const MetaData &c) {
    out << c.platformName << " " << c.appName << " " << c.appID << " "
        << c.sizeInBytes << " " << c.version;
    return out;
  }

  // create serialize function to serialize the class members
  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> buffer;
    buffer.reserve(sizeof(this->platformName) + sizeof(this->appName) +
                   sizeof(this->appID) + sizeof(this->sizeInBytes) +
                   sizeof(this->version));

    buffer.insert(buffer.end(), this->platformName.c_str(),
                  this->platformName.c_str() + this->platformName.length());
    buffer.push_back(0);

    buffer.insert(buffer.end(), this->appName.c_str(),
                  this->appName.c_str() + this->appName.length());
    buffer.push_back(0);

    buffer.insert(buffer.end(), this->appID.c_str(),
                  this->appID.c_str() + this->appID.length());
    buffer.push_back(0);

    buffer.insert(buffer.end(), this->version.c_str(),
                  this->version.c_str() + this->version.length());
    buffer.push_back(0);

    return buffer;
  }
  // create serialize function to deserialize the class members into json string
  // format
  std::string serializeToJson() const {

    nlohmann::json j;
    j["platformName"] = this->platformName;
    j["appName"] = this->appName;
    j["appID"] = this->appID;
    j["sizeInBytes"] = this->sizeInBytes;
    j["version"] = this->version;
    return j.dump();
  }
};
#endif