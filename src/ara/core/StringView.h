/**
 * @file StringView.h
 * @brief ara::core::StringView (SWS_CORE_02001) - maps to std::string_view (C++17).
 */
#ifndef ARA_CORE_STRING_VIEW_H_
#define ARA_CORE_STRING_VIEW_H_

#include <string_view>

namespace ara::core {
using StringView = std::basic_string_view<char>;
} // namespace ara::core

#endif // ARA_CORE_STRING_VIEW_H_
