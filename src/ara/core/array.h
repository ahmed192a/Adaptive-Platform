/** @file array.h  ara::core::Array (SWS_CORE_01201) -> std::array */
#ifndef ARA_CORE_ARRAY_H_
#define ARA_CORE_ARRAY_H_
#include <array>
#include <cstddef>
namespace ara::core {
template <typename T, std::size_t N> using Array = std::array<T, N>;
} // namespace ara::core
#endif
