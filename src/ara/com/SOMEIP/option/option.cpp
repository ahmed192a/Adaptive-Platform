/**
 * @file option.cpp
 * @author Flashing Adapter Graduation Project Team
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#include "ara/com/SOMEIP/option/option.h"

namespace ara::com::option {
OptionType Option::Type() const noexcept { return GBType; }

bool Option::Discardable() const noexcept { return GBDiscardable; }

std::vector<uint8_t> Option::BasePayload() const {
  std::vector<uint8_t> _result;

  helper::Inject(_result, Length());

  auto _type = static_cast<uint8_t>(GBType);
  _result.push_back(_type);

  auto _discardableFlag = static_cast<uint8_t>(GBDiscardable);
  _result.push_back(_discardableFlag);

  return _result;
}
} // namespace ara::com::option
