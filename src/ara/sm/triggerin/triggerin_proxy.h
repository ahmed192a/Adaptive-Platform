#ifndef SM_TRIGGERIN_PROXY_H_
#define SM_TRIGGERIN_PROXY_H_

#include "ara/com/proxy_skeleton/proxy/field.h"
#include "ara/com/proxy_skeleton/proxy/service_proxy.h"
#include "ara/sm/triggerin/triggerin_types.h"

namespace ara::sm::triggerin::proxy {
namespace fields {

using UCM_Trigger =
    ara::com::proxy_skeleton::proxy::FieldType<ara::sm::triggerin::UCM_State,
                                               true, true, true>::type;
using OTA_Trigger =
    ara::com::proxy_skeleton::proxy::FieldType<ara::sm::triggerin::OTA_State,
                                               true, true, true>::type;
} // namespace fields
class Trigger_In_UCM_Proxy
    : public ara::com::proxy_skeleton::proxy::ServiceProxy {
public:
  explicit Trigger_In_UCM_Proxy(
      ara::com::proxy_skeleton::proxy::ServiceProxy::SP_Handle proxy_handle)
      : ara::com::proxy_skeleton::proxy::ServiceProxy(proxy_handle),
        trigger(this, "trigger", 0) {}

  // Fields
  fields::UCM_Trigger trigger;
};
class Trigger_In_OTA_Proxy
    : public ara::com::proxy_skeleton::proxy::ServiceProxy {
public:
  explicit Trigger_In_OTA_Proxy(
      ara::com::proxy_skeleton::proxy::ServiceProxy::SP_Handle proxy_handle)
      : ara::com::proxy_skeleton::proxy::ServiceProxy(proxy_handle),
        trigger(this, "trigger", 0) {}

  // Fields
  fields::OTA_Trigger trigger;
};


template<typename T> class Trigger_In : public ara::com::proxy_skeleton::proxy::ServiceProxy{
public:
    explicit Trigger_In(ara::com::proxy_skeleton::proxy::ServiceProxy::SP_Handle const& proxy_handle)
        : ara::com::proxy_skeleton::proxy::ServiceProxy(proxy_handle),
          trigger(this, "trigger", 0) {}

    // Fields
    typename ara::com::proxy_skeleton::proxy::FieldType<T, true, true, true>::type trigger;
};
} // namespace ara::sm::triggerin::proxy

#endif // SM_TRIGGERIN_PROXY_H_