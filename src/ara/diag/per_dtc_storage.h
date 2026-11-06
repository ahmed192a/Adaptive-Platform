/**
 * @file per_dtc_storage.h
 * @brief Persistent DTC event memory on top of ara::per (see DiagnosticServer::SetDtcStorage).
 */
#ifndef ARA_DIAG_PER_DTC_STORAGE_H_
#define ARA_DIAG_PER_DTC_STORAGE_H_

#include <string>

#include "ara/diag/diagnostic_server.h"
#include "ara/per/persistency.h"

namespace ara::diag {

/// Text codec used for the stored value (exposed for tests): one line per DTC.
std::string EncodeDtcMemory(DtcMemory const &memory);
DtcMemory DecodeDtcMemory(std::string const &text);

/// Stores the event memory as one string value under @p key and syncs after every change.
DtcStorage MakePerDtcStorage(ara::per::SharedHandle<ara::per::KeyValueStorage> kvs,
                             std::string key = "ara.diag.dtcMemory");

} // namespace ara::diag

#endif // ARA_DIAG_PER_DTC_STORAGE_H_
