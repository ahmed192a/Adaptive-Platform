#include "ara/core/instance_specifier.h"

namespace ara::core {

namespace {
bool IsShortname(StringView s) {
  if (s.empty()) {
    return false;
  }
  auto const alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
  if (!alpha(s.front())) {
    return false;
  }
  for (char c : s) {
    if (!alpha(c) && !(c >= '0' && c <= '9') && c != '_') {
      return false;
    }
  }
  return true;
}
} // namespace

Result<void> InstanceSpecifier::Validate(StringView id) {
  if (id.empty()) {
    return Result<void>(MakeErrorCode(CoreErrc::kInvalidMetaModelShortname));
  }
  bool const isPath = id.find('/') != StringView::npos;
  while (true) {
    auto const pos = id.find('/');
    StringView const element = id.substr(0, pos);
    if (element.empty() && isPath) {
      return Result<void>(MakeErrorCode(CoreErrc::kInvalidMetaModelPath));
    }
    if (!IsShortname(element)) {
      return Result<void>(MakeErrorCode(CoreErrc::kInvalidMetaModelShortname));
    }
    if (pos == StringView::npos) {
      break;
    }
    id.remove_prefix(pos + 1);
    if (id.empty()) { // trailing slash
      return Result<void>(MakeErrorCode(CoreErrc::kInvalidMetaModelPath));
    }
  }
  return Result<void>();
}

Result<InstanceSpecifier> InstanceSpecifier::Create(StringView metaModelIdentifier) {
  auto const valid = Validate(metaModelIdentifier);
  if (!valid.HasValue()) {
    return Result<InstanceSpecifier>(valid.Error());
  }
  return Result<InstanceSpecifier>(InstanceSpecifier(Validated{}, metaModelIdentifier));
}

InstanceSpecifier::InstanceSpecifier(StringView metaModelIdentifier) : mId() {
  auto const valid = Validate(metaModelIdentifier);
  if (!valid.HasValue()) {
    ThrowOrTerminate<CoreException>(valid.Error());
  }
  mId = std::string(metaModelIdentifier);
}

} // namespace ara::core
