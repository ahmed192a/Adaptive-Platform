/** @file steady_clock.h  ara::core::SteadyClock (SWS_CORE_06401) */
#ifndef ARA_CORE_STEADY_CLOCK_H_
#define ARA_CORE_STEADY_CLOCK_H_
#include <chrono>
namespace ara::core {
/// Monotonic clock satisfying the C++ Clock requirements.
class SteadyClock final {
public:
  using rep = std::chrono::steady_clock::rep;
  using period = std::chrono::steady_clock::period;
  using duration = std::chrono::steady_clock::duration;
  using time_point = std::chrono::time_point<SteadyClock>;
  static constexpr bool is_steady = true;

  static time_point now() noexcept {
    return time_point(std::chrono::duration_cast<duration>(
        std::chrono::steady_clock::now().time_since_epoch()));
  }
};
} // namespace ara::core
#endif
