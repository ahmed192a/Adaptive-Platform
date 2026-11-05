/**
 * @file abort.h
 * @brief ara::core::Abort / SetAbortHandler (SWS_CORE_12402 ff. in R23-11 chapter 7).
 *
 * Signatures follow the SWS as recalled; chapter 8 could not be re-read for R25-11, so
 * they are tracked as "unverified" in doc/spec-conformance/core.md.
 */
#ifndef ARA_CORE_ABORT_H_
#define ARA_CORE_ABORT_H_

namespace ara::core {

/// Handler invoked by Abort() before the process is terminated.
using AbortHandler = void (*)() noexcept;

/// Installs a handler (nullptr restores the default); returns the previous one. Thread-safe.
AbortHandler SetAbortHandler(AbortHandler handler) noexcept;

/// Prints `text` to stderr, runs the abort handler (if any) and terminates the process.
[[noreturn]] void Abort(char const *text) noexcept;

} // namespace ara::core

#endif // ARA_CORE_ABORT_H_
