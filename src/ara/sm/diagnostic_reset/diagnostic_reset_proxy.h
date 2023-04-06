/**
 * @file diagnostic_reset_proxy.hpp
 * @author Flashing Adapter Graduation Project Team
 * @version 0.1
 * @date 2022-03-14
 *
 * @copyright Copyright (c) 2022
 *
 */

#include "ara/com/proxy_skeleton/proxy/field.h"
#include "ara/com/proxy_skeleton/proxy/service_proxy.h"
#include "ara/sm/diagnostic_reset/diagnostic_reset_common.h"

namespace ara::sm::diagnostic_reset::proxy {
namespace methods {
using message_diagnostic = ara::sm::diagnostic_reset::message_diagnostic;
using event_diagnostic = ara::sm::diagnostic_reset::event_diagnostic;
} // namespace methods

class DiagnosticReset : public ara::com::proxy_skeleton::proxy::ServiceProxy {
public:
  explicit DiagnosticReset(
      ara::com::proxy_skeleton::proxy::ServiceProxy::SP_Handle proxy_handle);

  methods::event_diagnostic event_diagnostic;
  methods::message_diagnostic message_diagnostic;
};
} // namespace ara::sm::diagnostic_reset::proxy
