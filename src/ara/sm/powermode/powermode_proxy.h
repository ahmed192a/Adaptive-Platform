/**
 * @file powermode_proxy.hpp
 * @author Flashing Adapter Graduation Project Team
 * @version 0.1
 * @date 2022-03-14
 *
 * @copyright Copyright (c) 2022
 *
 */

#include "ara/com/proxy_skeleton/proxy/field.h"
#include "ara/com/proxy_skeleton/proxy/service_proxy.h"
#include "powermode_common.h"

namespace ara::sm::powermode::proxy {

class PowerMode : public ara::com::proxy_skeleton::proxy::ServiceProxy {
public:
  explicit PowerMode(
      ara::com::proxy_skeleton::proxy::ServiceProxy::SP_Handle const& proxy_handle)
      : ara::com::proxy_skeleton::proxy::ServiceProxy(proxy_handle),
        MessagePowerMode(this), EventPowerMode(this) {}
  ~PowerMode() override =default;

  ara::sm::powermode::proxy::methods::MessagePowerMode MessagePowerMode;
  ara::sm::powermode::proxy::methods::MessagePowerMode EventPowerMode;
};
} // namespace ara::sm::powermode::proxy
