/**
 * @file phm_config.h
 * @brief PHM deployment configuration (JSON stand-in for the ARXML PHM manifest).
 *
 * @code{.json}
 * {
 *   "supervisions": [
 *     {"name":"AliveA","type":"alive","entity":"App/E","checkpoint":1,
 *      "expectedAliveIndications":3,"minMargin":1,"maxMargin":1,
 *      "referenceCycleMs":100,"failedCyclesTolerance":2},
 *     {"name":"DlA","type":"deadline","entity":"App/E","startCheckpoint":1,
 *      "endCheckpoint":2,"minMs":5,"maxMs":50},
 *     {"name":"LogA","type":"logical","entity":"App/E","checkpoints":[1,2,3],
 *      "initial":[1],"final":[3],"transitions":[[1,2],[2,3]]}
 *   ],
 *   "globalSupervisions": [
 *     {"name":"G1","members":["AliveA","DlA","LogA"],"functionGroup":"MachineFG",
 *      "activeStates":["Running"],"recoveryAction":"App/Recovery",
 *      "expiredToleranceMs":0,"platformCritical":false,"executionError":1}
 *   ]
 * }
 * @endcode
 */
#ifndef ARA_PHM_PHM_CONFIG_H_
#define ARA_PHM_PHM_CONFIG_H_

#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ara/core/result.h"
#include "ara/phm/supervision.h"

namespace ara::phm {

struct GlobalSupervisionConfig {
  std::string name;
  std::vector<std::string> members;     ///< elementary supervision names
  std::string functionGroup;            ///< empty = always active
  std::set<std::string> activeStates;   ///< empty = active in every state
  std::string recoveryAction;           ///< InstanceSpecifier of the RecoveryAction
  Duration expiredTolerance{Duration::zero()};
  bool platformCritical{false};         ///< SM/EM/OS supervision: EXPIRED -> STOPPED
  bool escalateToWatchdog{true};        ///< fire watchdog if recovery is impossible
  std::uint32_t executionError{1}; ///< SWS_PHM_01361: 1 when none is configured
};

struct PhmConfig {
  std::vector<std::shared_ptr<ElementarySupervision>> supervisions;
  std::vector<GlobalSupervisionConfig> globals;

  static ara::core::Result<PhmConfig> FromJson(std::string const &json);
  static ara::core::Result<PhmConfig> FromFile(std::filesystem::path const &file);
};

} // namespace ara::phm

#endif // ARA_PHM_PHM_CONFIG_H_
