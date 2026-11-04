/**
 * @file initialization.h
 * @brief ara::core::Initialize() / Deinitialize() (SWS_CORE_10001, SWS_CORE_10002)
 *
 * Initialize() brings up the AUTOSAR Adaptive runtime of the process, i.e.
 * every functional cluster that registered a lifecycle hook.  Clusters
 * register themselves through ARA_REGISTER_LIFECYCLE_HOOK().
 */
#ifndef ARA_CORE_INITIALIZATION_H_
#define ARA_CORE_INITIALIZATION_H_

#include "ara/core/core_error_domain.h"
#include "ara/core/result.h"

namespace ara::core {

/// Initialize the runtime. Must be the first ara:: call of the process.
Result<void> Initialize();
/// Tear the runtime down (reverse order of Initialize()).
Result<void> Deinitialize();
/// True between a successful Initialize() and the following Deinitialize().
bool IsInitialized() noexcept;

namespace detail {
struct LifecycleHook {
  char const *name;
  Result<void> (*init)();
  Result<void> (*deinit)();
};
/// Registers a hook (idempotent per name).  Hooks run in registration order.
bool RegisterLifecycleHook(LifecycleHook hook);
} // namespace detail

} // namespace ara::core

#define ARA_CORE_CONCAT_(a, b) a##b
#define ARA_CORE_CONCAT(a, b) ARA_CORE_CONCAT_(a, b)
/// Place in a .cpp of a cluster: ARA_REGISTER_LIFECYCLE_HOOK("ara::log", &InitLog, &DeinitLog);
#define ARA_REGISTER_LIFECYCLE_HOOK(NAME, INIT, DEINIT)                                     \
  namespace {                                                                               \
  [[maybe_unused]] const bool ARA_CORE_CONCAT(kAraHook_, __LINE__) =                        \
      ::ara::core::detail::RegisterLifecycleHook({NAME, INIT, DEINIT});                     \
  }

#endif // ARA_CORE_INITIALIZATION_H_
