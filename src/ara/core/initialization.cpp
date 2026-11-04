#include "ara/core/initialization.h"

#include <algorithm>
#include <mutex>
#include <string_view>
#include <vector>

namespace ara::core {

namespace {
struct Registry {
  std::mutex mutex;
  std::vector<detail::LifecycleHook> hooks;
  bool initialized{false};
};
Registry &Reg() {
  static Registry r; // function-local: safe against static-init order
  return r;
}
} // namespace

namespace detail {
bool RegisterLifecycleHook(LifecycleHook hook) {
  auto &r = Reg();
  std::lock_guard<std::mutex> lock(r.mutex);
  auto const same = [&](LifecycleHook const &h) {
    return std::string_view(h.name) == std::string_view(hook.name);
  };
  if (std::any_of(r.hooks.begin(), r.hooks.end(), same)) {
    return false;
  }
  r.hooks.push_back(hook);
  return true;
}
} // namespace detail

Result<void> Initialize() {
  auto &r = Reg();
  std::lock_guard<std::mutex> lock(r.mutex);
  if (r.initialized) {
    return Result<void>(MakeErrorCode(CoreErrc::kAlreadyInitialized));
  }
  for (std::size_t i = 0; i < r.hooks.size(); ++i) {
    if (r.hooks[i].init == nullptr) {
      continue;
    }
    auto const res = r.hooks[i].init();
    if (!res.HasValue()) {
      for (std::size_t j = i; j-- > 0;) { // roll back what was already started
        if (r.hooks[j].deinit != nullptr) {
          (void)r.hooks[j].deinit();
        }
      }
      return Result<void>(MakeErrorCode(CoreErrc::kInitializationFailed,
                                        static_cast<ErrorDomain::SupportDataType>(i),
                                        r.hooks[i].name));
    }
  }
  r.initialized = true;
  return Result<void>();
}

Result<void> Deinitialize() {
  auto &r = Reg();
  std::lock_guard<std::mutex> lock(r.mutex);
  if (!r.initialized) {
    return Result<void>(MakeErrorCode(CoreErrc::kNotInitialized));
  }
  r.initialized = false;
  for (std::size_t i = r.hooks.size(); i-- > 0;) {
    if (r.hooks[i].deinit != nullptr) {
      (void)r.hooks[i].deinit();
    }
  }
  return Result<void>();
}

bool IsInitialized() noexcept {
  auto &r = Reg();
  std::lock_guard<std::mutex> lock(r.mutex);
  return r.initialized;
}

} // namespace ara::core
