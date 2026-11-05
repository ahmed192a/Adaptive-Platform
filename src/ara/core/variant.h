/** @file variant.h  ara::core::Variant -> std::variant, with the helper traits of the SWS */
#ifndef ARA_CORE_VARIANT_H_
#define ARA_CORE_VARIANT_H_
#include <variant>
namespace ara::core {
template <typename... Ts> using Variant = std::variant<Ts...>;
using monostate = std::monostate;
using bad_variant_access = std::bad_variant_access;
inline constexpr std::size_t variant_npos = std::variant_npos;
template <typename V> using variant_size = std::variant_size<V>;
template <typename V> inline constexpr std::size_t variant_size_v = std::variant_size_v<V>;
template <std::size_t I, typename V> using variant_alternative = std::variant_alternative<I, V>;
template <std::size_t I, typename V> using variant_alternative_t = std::variant_alternative_t<I, V>;
using std::get;
using std::get_if;
using std::holds_alternative;
using std::visit;
} // namespace ara::core
#endif
