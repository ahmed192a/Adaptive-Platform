#include "ara/phm/recovery_action.h"

namespace ara::phm {

ara::core::Result<void> RecoveryAction::Offer() {
  auto r = mMonitor->OfferRecoveryAction(mName, mHandler);
  if (r.HasValue()) {
    mOffered = true;
  }
  return r;
}

void RecoveryAction::StopOffer() noexcept {
  if (mOffered) {
    mMonitor->StopOfferRecoveryAction(mName);
    mOffered = false;
  }
}

} // namespace ara::phm
