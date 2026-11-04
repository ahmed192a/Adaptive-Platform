#include "ara/phm/phm_types.h"

namespace ara::phm {

char const *ToString(TypeOfSupervision t) noexcept {
  switch (t) {
  case TypeOfSupervision::kAliveSupervision: return "alive";
  case TypeOfSupervision::kDeadlineSupervision: return "deadline";
  case TypeOfSupervision::kLogicalSupervision: return "logical";
  }
  return "unknown";
}
char const *ToString(ElementarySupervisionStatus s) noexcept {
  switch (s) {
  case ElementarySupervisionStatus::kOK: return "OK";
  case ElementarySupervisionStatus::kFAILED: return "FAILED";
  case ElementarySupervisionStatus::kEXPIRED: return "EXPIRED";
  case ElementarySupervisionStatus::kDEACTIVATED: return "DEACTIVATED";
  }
  return "unknown";
}
char const *ToString(GlobalSupervisionStatus s) noexcept {
  switch (s) {
  case GlobalSupervisionStatus::kOK: return "OK";
  case GlobalSupervisionStatus::kFAILED: return "FAILED";
  case GlobalSupervisionStatus::kEXPIRED: return "EXPIRED";
  case GlobalSupervisionStatus::kSTOPPED: return "STOPPED";
  case GlobalSupervisionStatus::kDEACTIVATED: return "DEACTIVATED";
  }
  return "unknown";
}

} // namespace ara::phm
