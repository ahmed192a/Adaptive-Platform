/** @file optional.h  ara::core::Optional (SWS_CORE_01033) -> std::optional */
#ifndef ARA_CORE_OPTIONAL_H_
#define ARA_CORE_OPTIONAL_H_
#include <optional>
namespace ara::core {
template <typename T> using Optional = std::optional<T>;
using nullopt_t = std::nullopt_t;
inline constexpr nullopt_t nullopt = std::nullopt;
using bad_optional_access = std::bad_optional_access;
} // namespace ara::core
#endif
