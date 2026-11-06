/**
 * @file execution_client.hpp
 * @author Flashing Adapter Graduation Project Team
 * @brief Definition of the execution client class
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef ARA_EXEC_EXECUTION_CLIENT_H_
#define ARA_EXEC_EXECUTION_CLIENT_H_

#include "ara/core/result.h"
#include "ara/exec/exec_error_domain.h"
#include <atomic>
#include <functional>
#include <cstdint>
#include <string>
#include <variant>

namespace ara::exec {

/**
 * @brief ExecutionState Defines the proxy_skeleton states of a Process
 * (see 7.3.1). Scoped Enumeration of uint8_t .
 * @note SWS_EM_02000
 */
enum class ExecutionState : uint8_t {
  kRunning = 0, /*!< After a Process has been started by Execution Management,
                   it reports ExecutionState kRunning. */
  kTerminating =
      1, /*!< On receipt of SIGTERM, a Reporting Process acknowledges the
            request (by reporting kTerminating to Execution Management. */
};

/**
 * @brief ExecutionClient Class to implement operations on Execution Client.
 * @note SWS_EM_02001
 */
class ExecutionClient final {
private:
  std::string fifo_l{"execution_client_fifo"}; /*!< location of FIFO to
                                                communicate between EM & SM */
  int fd{-1};                                  /*!< index of File descriptor */
  /// SWS_EM_02243: kRunning is only valid while the process is in the Starting state.
  mutable std::atomic<bool> mRunningReported{false};
  /// True for instances made by Create(): they own the per-process slot (SWS_EM_02582).
  bool mOwnsSlot{false};

public:
  /**
   * @brief Constructor that creates the Execution Client.
   * @note SWS_EM_02030
   */
  ExecutionClient() noexcept;

  /**
   * @brief Spec-shaped factory (SWS_EM_02577): creates the one ExecutionClient of this process and
   *        installs a SIGTERM handler that runs @p terminationHandler on a background thread.
   * @return the client, or an error when a Create()d client already exists in this process
   *         (ExecErrc::kGeneralError, SWS_EM_02582) or the handler is empty (kInvalidArguments).
   * @note The legacy default constructor stays available and unrestricted: several platform processes
   *       still hold more than one client. Only Create()d clients are subject to the single-instance rule.
   *       The exact signature of the spec's Create is Unverified (see doc/spec-conformance/exec.md).
   */
  static ara::core::Result<ExecutionClient> Create(std::function<void()> terminationHandler) noexcept;

  ExecutionClient(ExecutionClient &&other) noexcept;
  ExecutionClient &operator=(ExecutionClient &&other) noexcept;
  ExecutionClient(ExecutionClient const &) = delete;
  ExecutionClient &operator=(ExecutionClient const &) = delete;

  /**
   * @brief Destructor of the Execution Client instance.
   * @note SWS_EM_02002
   */
  ~ExecutionClient() noexcept;

  /**
   * @brief Interface for a Process to report its proxy_skeleton state to
   * Execution Management.
   * @note SWS_EM_02003
   * @param [in] state     Value of the Execution State
   *
   * @return          An instance of Boost::varient2::variant<void, ExecErrc>.
   *                  - The instance holds an ExecErrc containing either
   *                      -ara::exec::ExecErrc::kGeneralError     if some
   * unspecified error occurred -ara::exec::ExecErrc::kCommunication    Error
   * Communication error between Application and Execution Management, e.g.
   * unable to report state for Non-reporting Process.
   */
  std::variant<std::monostate, ExecErrc>
  ReportExecutionState(ExecutionState state) const noexcept;

  /**
   * @brief Same behaviour as ReportExecutionState but with the spec's ara::core::Result<void> return type.
   */
  ara::core::Result<void> ReportExecutionStateResult(ExecutionState state) const noexcept;
};
} // namespace ara::exec

#endif // ARA_EXEC_EXECUTION_CLIENT_H_