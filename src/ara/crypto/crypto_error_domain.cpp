#include "ara/crypto/crypto_error_domain.h"

namespace ara::crypto {

char const *CryptoErrorDomain::Message(CodeType errorCode) const noexcept {
  switch (static_cast<CryptoErrc>(errorCode)) {
  case CryptoErrc::kInvalidArgument: return "invalid argument";
  case CryptoErrc::kUnknownIdentifier: return "unknown identifier";
  case CryptoErrc::kInsufficientCapacity: return "insufficient capacity";
  case CryptoErrc::kInvalidInputSize: return "invalid input size";
  case CryptoErrc::kIncompatibleArguments: return "incompatible arguments";
  case CryptoErrc::kInOutBuffersIntersect: return "input and output buffers intersect";
  case CryptoErrc::kEmptyContainer: return "empty container";
  case CryptoErrc::kUninitializedContext: return "context is not initialized";
  case CryptoErrc::kProcessingNotStarted: return "processing not started";
  case CryptoErrc::kProcessingNotFinished: return "processing not finished";
  case CryptoErrc::kUnsupported: return "unsupported";
  case CryptoErrc::kAuthTagNotValid: return "authentication failed";
  case CryptoErrc::kUsageViolation: return "key usage violation";
  case CryptoErrc::kKeyNotExportable: return "key is not exportable";
  case CryptoErrc::kRuntimeFault: return "crypto provider failure";
  case CryptoErrc::kInvalidKeyLength: return "invalid key length";
  case CryptoErrc::kBadObjectReference: return "bad object reference";
  case CryptoErrc::kBusyResource: return "resource is busy";
  case CryptoErrc::kAlgIdUndefined: return "algorithm id undefined";
  }
  return "unknown crypto error";
}

void CryptoErrorDomain::ThrowAsException(ara::core::ErrorCode const &errorCode) const
    noexcept(false) {
  ara::core::ThrowOrTerminate<CryptoException>(errorCode);
  std::terminate();
}

} // namespace ara::crypto
