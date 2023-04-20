/**
 * @file diagnostic_reset_return_types.hpp
 * @author Flashing Adapter Graduation Project Team
 * @brief
 * @version 0.1
 * @date 2022-03-14
 *
 * @copyright Copyright (c) 2022
 *
 */

#ifndef _DIAGNOSTIC_RESET_RETURN_TYPES_H_
#define _DIAGNOSTIC_RESET_RETURN_TYPES_H_

#include "ara/sm/diagnostic_reset/diagnostic_reset_types.h"
#include <variant>

namespace ara::sm::diagnostic_reset {

using MessageDiagnosticOutput = std::variant<DiagnosticResetMsg, std::monostate>;
using EventDiagnosticOutput = std::variant<DiagnosticResetRespMsg, std::monostate>;

} // namespace ara::sm::diagnostic_reset

#endif