/**
 * @file memory_resource.h
 * @brief ara::core::MemoryResource and the default-resource accessors, mapped to std::pmr.
 *        (SWS_CORE_11950 ff. in R23-11 chapter 7; chapter 8 signatures unverified.)
 */
#ifndef ARA_CORE_MEMORY_RESOURCE_H_
#define ARA_CORE_MEMORY_RESOURCE_H_
#include <memory_resource>
namespace ara::core {
using MemoryResource = std::pmr::memory_resource;
inline MemoryResource *GetDefaultResource() noexcept { return std::pmr::get_default_resource(); }
inline MemoryResource *SetDefaultResource(MemoryResource *r) noexcept {
  return std::pmr::set_default_resource(r);
}
inline MemoryResource *NewDeleteResource() noexcept { return std::pmr::new_delete_resource(); }
inline MemoryResource *NullMemoryResource() noexcept { return std::pmr::null_memory_resource(); }
} // namespace ara::core
#endif
