/**
 * @file phm_types.h
 * @brief Basic types of ara::phm (Platform Health Management)
 */
#ifndef ARA_PHM_PHM_TYPES_H_
#define ARA_PHM_PHM_TYPES_H_

#include <chrono>
#include <cstdint>
#include <string>

#include "ara/core/steady_clock.h"

namespace ara::phm {

/// Identifier of a checkpoint inside one SupervisedEntity.
using CheckpointType = std::uint32_t;
using TimePoint = ara::core::SteadyClock::time_point;
using Duration = std::chrono::steady_clock::duration;

enum class TypeOfSupervision : std::uint8_t {
  kAliveSupervision = 0,
  kDeadlineSupervision = 1,
  kLogicalSupervision = 2,
};

/// Status of one elementary supervision (SWS_PHM: ElementarySupervisionStatus).
enum class ElementarySupervisionStatus : std::uint8_t {
  kOK = 0,
  kFAILED = 1,      ///< temporary failure (alive supervision only)
  kEXPIRED = 2,     ///< failure confirmed
  kDEACTIVATED = 3, ///< supervision currently not active
};

/// Status of a global supervision (SWS_PHM_00216).
enum class GlobalSupervisionStatus : std::uint8_t {
  kOK = 0,
  kFAILED = 1,
  kEXPIRED = 2,
  kSTOPPED = 3,     ///< platform critical supervision expired; watchdog reaction fired
  kDEACTIVATED = 4,
};

char const *ToString(TypeOfSupervision t) noexcept;
char const *ToString(ElementarySupervisionStatus s) noexcept;
char const *ToString(GlobalSupervisionStatus s) noexcept;

} // namespace ara::phm

#endif // ARA_PHM_PHM_TYPES_H_
