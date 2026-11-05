#include "ara/nm/nm_types.h"

namespace ara::nm {

char const *ToString(NmState s) noexcept {
  switch (s) {
  case NmState::kBusSleep: return "BusSleep";
  case NmState::kPrepareBusSleep: return "PrepareBusSleep";
  case NmState::kReadySleep: return "ReadySleep";
  case NmState::kNormalOperation: return "NormalOperation";
  case NmState::kRepeatMessage: return "RepeatMessage";
  }
  return "?";
}

} // namespace ara::nm
