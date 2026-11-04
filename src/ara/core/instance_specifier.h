/**
 * @file instance_specifier.h
 * @brief ara::core::InstanceSpecifier (SWS_CORE_08001 ff.)
 *
 * Identifies a PortPrototype by the shortname path in the application manifest,
 * e.g. "MyApp/RPort_Persistency".  Valid values are non-empty paths of
 * shortnames ([A-Za-z][A-Za-z0-9_]*) separated by single '/' characters,
 * without leading or trailing '/'.
 */
#ifndef ARA_CORE_INSTANCE_SPECIFIER_H_
#define ARA_CORE_INSTANCE_SPECIFIER_H_

#include <functional>
#include <string>

#include "ara/core/StringView.h"
#include "ara/core/core_error_domain.h"
#include "ara/core/result.h"

namespace ara::core {

class InstanceSpecifier final {
public:
  /// Validating factory (SWS_CORE_08032).
  static Result<InstanceSpecifier> Create(StringView metaModelIdentifier);

  /// Throws CoreException (or terminates) when the identifier is invalid (SWS_CORE_08021).
  explicit InstanceSpecifier(StringView metaModelIdentifier);

  InstanceSpecifier(InstanceSpecifier const &) = default;
  InstanceSpecifier(InstanceSpecifier &&) noexcept = default;
  InstanceSpecifier &operator=(InstanceSpecifier const &) = default;
  InstanceSpecifier &operator=(InstanceSpecifier &&) noexcept = default;
  ~InstanceSpecifier() noexcept = default;

  StringView ToString() const noexcept { return mId; }

  bool operator==(InstanceSpecifier const &other) const noexcept { return mId == other.mId; }
  bool operator==(StringView other) const noexcept { return StringView(mId) == other; }
  bool operator!=(InstanceSpecifier const &other) const noexcept { return mId != other.mId; }
  bool operator!=(StringView other) const noexcept { return StringView(mId) != other; }
  bool operator<(InstanceSpecifier const &other) const noexcept { return mId < other.mId; }

private:
  struct Validated {};
  InstanceSpecifier(Validated, StringView id) : mId(id) {}
  static Result<void> Validate(StringView id);

  std::string mId;
};

} // namespace ara::core

namespace std {
template <> struct hash<ara::core::InstanceSpecifier> {
  size_t operator()(ara::core::InstanceSpecifier const &s) const noexcept {
    return hash<ara::core::StringView>()(s.ToString());
  }
};
} // namespace std

#endif // ARA_CORE_INSTANCE_SPECIFIER_H_
