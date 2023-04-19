#ifndef SM_TRIGGERIN_PROXY_H_
#define SM_TRIGGERIN_PROXY_H_

#include "ara/com/proxy_skeleton/proxy/field.h"
#include "ara/com/proxy_skeleton/proxy/service_proxy.h"
#include "ara/sm/triggerin/triggerin_types.h"

namespace ara::sm::triggerin::proxy {

template <typename T>
class Trigger_In : public ara::com::proxy_skeleton::proxy::ServiceProxy {
public:
  explicit Trigger_In(
      ara::com::proxy_skeleton::proxy::ServiceProxy::SP_Handle const
          &proxy_handle)
      : ara::com::proxy_skeleton::proxy::ServiceProxy(proxy_handle),
        trigger(this, "trigger", 0) {}

  // Fields
  typename ara::com::proxy_skeleton::proxy::FieldType<T, true, true, true>::type
      trigger;
};
} // namespace ara::sm::triggerin::proxy

#endif // SM_TRIGGERIN_PROXY_H_