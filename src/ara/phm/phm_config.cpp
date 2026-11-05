#include "ara/phm/phm_config.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "ara/phm/phm_error_domain.h"

namespace ara::phm {

using ara::core::Result;
using nlohmann::json;

namespace {
Result<PhmConfig> Bad(char const *why) {
  return Result<PhmConfig>(MakeErrorCode(PhmErrc::kInvalidConfiguration, 0, why));
}
Duration Ms(json const &j, char const *key, std::int64_t def) {
  return std::chrono::milliseconds(j.value(key, def));
}
} // namespace

Result<PhmConfig> PhmConfig::FromJson(std::string const &text) {
  json doc = json::parse(text, nullptr, false);
  if (doc.is_discarded() || !doc.is_object()) {
    return Bad("PHM config is not a JSON object");
  }
  PhmConfig cfg;
  try {
    for (auto const &s : doc.value("supervisions", json::array())) {
      std::string const name = s.at("name").get<std::string>();
      std::string const type = s.at("type").get<std::string>();
      std::string const entity = s.at("entity").get<std::string>();
      if (type == "alive") {
        AliveSupervision::Config c;
        c.checkpoint = {entity, s.at("checkpoint").get<CheckpointType>()};
        c.expectedAliveIndications = s.value("expectedAliveIndications", 1U);
        c.minMargin = s.value("minMargin", 0U);
        c.maxMargin = s.value("maxMargin", 0U);
        c.referenceCycle = Ms(s, "referenceCycleMs", 100);
        c.failedReferenceCyclesTolerance = s.value("failedCyclesTolerance", 0U);
        if (c.referenceCycle <= Duration::zero()) {
          return Bad("referenceCycleMs must be > 0");
        }
        cfg.supervisions.push_back(std::make_shared<AliveSupervision>(name, c));
      } else if (type == "deadline") {
        DeadlineSupervision::Config c;
        c.start = {entity, s.at("startCheckpoint").get<CheckpointType>()};
        c.end = {entity, s.at("endCheckpoint").get<CheckpointType>()};
        c.minDeadline = Ms(s, "minMs", 0);
        c.maxDeadline = Ms(s, "maxMs", 100);
        if (c.minDeadline > c.maxDeadline) {
          return Bad("deadline minMs > maxMs");
        }
        cfg.supervisions.push_back(std::make_shared<DeadlineSupervision>(name, c));
      } else if (type == "logical") {
        LogicalSupervision::Config c;
        c.entity = entity;
        for (auto v : s.at("checkpoints")) c.checkpoints.insert(v.get<CheckpointType>());
        for (auto v : s.at("initial")) c.initial.insert(v.get<CheckpointType>());
        for (auto v : s.at("final")) c.final.insert(v.get<CheckpointType>());
        for (auto const &t : s.at("transitions")) {
          c.transitions.insert({t.at(0).get<CheckpointType>(), t.at(1).get<CheckpointType>()});
        }
        for (auto v : c.initial) {
          if (c.checkpoints.count(v) == 0) return Bad("initial checkpoint not in 'checkpoints'");
        }
        cfg.supervisions.push_back(std::make_shared<LogicalSupervision>(name, c));
      } else {
        return Bad("supervision type must be alive, deadline or logical");
      }
    }
    for (auto const &g : doc.value("globalSupervisions", json::array())) {
      GlobalSupervisionConfig c;
      c.name = g.at("name").get<std::string>();
      c.members = g.at("members").get<std::vector<std::string>>();
      c.functionGroup = g.value("functionGroup", "");
      for (auto const &st : g.value("activeStates", json::array())) c.activeStates.insert(st.get<std::string>());
      c.recoveryAction = g.value("recoveryAction", "");
      c.expiredTolerance = Ms(g, "expiredToleranceMs", 0);
      c.platformCritical = g.value("platformCritical", false);
      c.escalateToWatchdog = g.value("escalateToWatchdog", true);
      c.executionError = g.value("executionError", 1U);
      cfg.globals.push_back(std::move(c));
    }
  } catch (json::exception const &) {
    return Bad("missing or mistyped field in PHM config");
  }
  return Result<PhmConfig>(std::move(cfg));
}

Result<PhmConfig> PhmConfig::FromFile(std::filesystem::path const &file) {
  std::ifstream in(file);
  if (!in) {
    return Bad("cannot open PHM config file");
  }
  std::stringstream ss;
  ss << in.rdbuf();
  return FromJson(ss.str());
}

} // namespace ara::phm
