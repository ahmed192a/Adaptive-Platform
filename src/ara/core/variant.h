/** @file variant.h  ara::core::Variant (SWS_CORE_01601) -> std::variant */
#ifndef ARA_CORE_VARIANT_H_
#define ARA_CORE_VARIANT_H_
#include <variant>
namespace ara::core {
template <typename... Ts> using Variant = std::variant<Ts...>;
using monostate = std::monostate;
using std::get;
using std::get_if;
using std::holds_alternative;
using std::visit;
} // namespace ara::core
#endif
