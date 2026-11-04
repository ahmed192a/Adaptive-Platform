/** @file map.h  ara::core::Map (SWS_CORE_01400) -> std::map */
#ifndef ARA_CORE_MAP_H_
#define ARA_CORE_MAP_H_
#include <functional>
#include <map>
#include <memory>
namespace ara::core {
template <typename K, typename V, typename C = std::less<K>,
          typename A = std::allocator<std::pair<const K, V>>>
using Map = std::map<K, V, C, A>;
} // namespace ara::core
#endif
