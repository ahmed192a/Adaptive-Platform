/**
 * @file powermode_return_types.hpp
 * @author Flashing Adapter Graduation Project Team
 * @version 0.1
 * @date 2022-03-14
 *
 * @copyright Copyright (c) 2022
 *
 */

#ifndef RETURN_TYPES_H_
#define RETURN_TYPES_H_

#include "powermode_types.h"
#include <variant>

namespace ara::sm::powermode {

using MessagePowermodeOutput = std::variant<PowerModeMsg, std::monostate>;
using EventPowermodeOutput = std::variant<PowerModeRespMsg, std::monostate>;

} // namespace ara::sm::powermode

#endif