#ifndef ARA_PER_CRC32_H_
#define ARA_PER_CRC32_H_
#include <array>
#include <cstdint>
#include <string_view>
namespace ara::per::detail {
/// CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320).
inline std::uint32_t Crc32(std::string_view data) noexcept {
  static const auto table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1U) != 0U ? 0xEDB88320U ^ (c >> 1U) : c >> 1U;
      }
      t[i] = c;
    }
    return t;
  }();
  std::uint32_t crc = 0xFFFFFFFFU;
  for (char ch : data) {
    crc = table[(crc ^ static_cast<std::uint8_t>(ch)) & 0xFFU] ^ (crc >> 8U);
  }
  return crc ^ 0xFFFFFFFFU;
}
} // namespace ara::per::detail
#endif
