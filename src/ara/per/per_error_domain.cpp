#include "ara/per/per_error_domain.h"

namespace ara::per {

char const *PerErrorDomain::Message(CodeType errorCode) const noexcept {
  switch (static_cast<PerErrc>(errorCode)) {
  case PerErrc::kStorageNotFound: return "storage not found";
  case PerErrc::kKeyNotFound: return "key not found";
  case PerErrc::kIllegalWriteAccess: return "illegal write access";
  case PerErrc::kPhysicalStorageFailure: return "physical storage failure";
  case PerErrc::kIntegrityCorrupted: return "integrity check failed";
  case PerErrc::kValidationFailed: return "validation failed";
  case PerErrc::kEncryptionFailed: return "encryption failed";
  case PerErrc::kDataTypeMismatch: return "stored value has a different data type";
  case PerErrc::kInitValueNotAvailable: return "initial value not available";
  case PerErrc::kResourceBusy: return "resource busy";
  case PerErrc::kOutOfStorageSpace: return "out of storage space";
  case PerErrc::kFileNotFound: return "file not found";
  case PerErrc::kEof: return "end of file";
  case PerErrc::kTooManyFiles: return "too many files";
  case PerErrc::kQuotaExceeded: return "storage quota exceeded";
  case PerErrc::kInvalidPosition: return "invalid position";
  case PerErrc::kInvalidOpenMode: return "invalid open mode";
  case PerErrc::kInvalidSize: return "invalid size";
  case PerErrc::kNotInitialized: return "persistency is not configured";
  case PerErrc::kUnsupported: return "unsupported operation";
  case PerErrc::kAuthenticationFailed: return "authentication failed";
  }
  return "unknown persistency error";
}

void PerErrorDomain::ThrowAsException(ara::core::ErrorCode const &errorCode) const
    noexcept(false) {
  ara::core::ThrowOrTerminate<PerException>(errorCode);
  std::terminate();
}

} // namespace ara::per
