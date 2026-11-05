#include "ara/core/abort.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace ara::core {

namespace {
std::atomic<AbortHandler> gHandler{nullptr};
}

AbortHandler SetAbortHandler(AbortHandler handler) noexcept { return gHandler.exchange(handler); }

void Abort(char const *text) noexcept {
  std::fprintf(stderr, "ara::core::Abort: %s\n", text != nullptr ? text : "");
  if (AbortHandler const h = gHandler.load(); h != nullptr) {
    h();
  }
  std::abort();
}

} // namespace ara::core
