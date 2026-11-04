/** @file vector.h  ara::core::Vector (SWS_CORE_01901) -> std::vector */
#ifndef ARA_CORE_VECTOR_H_
#define ARA_CORE_VECTOR_H_
#include <memory>
#include <vector>
namespace ara::core {
template <typename T, typename Allocator = std::allocator<T>>
using Vector = std::vector<T, Allocator>;
} // namespace ara::core
#endif
