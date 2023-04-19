/**
 * @file triggerinout_proxy.hpp
 * @author Flashing Adapter Graduation Project Team
 * @version 0.1
 * @date 2022-03-14
 *
 * @copyright Copyright (c) 2022
 *
 */
#include "ara/com/proxy_skeleton/proxy/field.h"
#include "ara/com/proxy_skeleton/proxy/service_proxy.h"

namespace ara::sm::triggerinout::proxy {

template<typename T>
class Trigger_InOut_Proxy
    : public ara::com::proxy_skeleton::proxy::ServiceProxy {
public:
  explicit Trigger_InOut_Proxy(
      ara::com::proxy_skeleton::proxy::ServiceProxy::SP_Handle proxy_handle)
      : ara::com::proxy_skeleton::proxy::ServiceProxy(proxy_handle),
        Notifier(this, "Notifier", 0), Trigger(this, "Trigger", 1) {}
  ~Trigger_InOut_Proxy() = default;
  
  typename ara::com::proxy_skeleton::proxy::FieldType<T, true, true, true>::type Notifier;
    typename ara::com::proxy_skeleton::proxy::FieldType<T, true, true, true>::type Trigger;
  
};
} // namespace ara::sm::triggerinout::proxy