#include "ara/com/proxy_skeleton/proxy/field.h"
#include "ara/com/proxy_skeleton/proxy/service_proxy.h"

namespace ara::sm::triggerout::proxy {
template <typename T>
class Trigger_Out_Proxy : public ara::com::proxy_skeleton::proxy::ServiceProxy {
public:
  explicit Trigger_Out_Proxy(
      ara::com::proxy_skeleton::proxy::ServiceProxy::SP_Handle proxy_handle)
      : ara::com::proxy_skeleton::proxy::ServiceProxy(proxy_handle),
        Notifier(this, "Notifier", 0) {}
  ~Trigger_Out_Proxy() = default;
  typename ara::com::proxy_skeleton::proxy::FieldType<T, true, true, true>::type
      Notifier;
};
} // namespace ara::sm::triggerout::proxy
