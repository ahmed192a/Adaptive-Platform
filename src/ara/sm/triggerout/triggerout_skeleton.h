/**
 * @file triggerout_skeleton.hpp
 * @author Flashing Adapter Graduation Project Team
 * @brief
 * @version 0.1
 * @date 2022-03-14
 *
 * @copyright Copyright (c) 2022
 *
 */

#include "ara/com/proxy_skeleton/definitions.h"
#include "ara/com/proxy_skeleton/skeleton/field.h"
#include "ara/com/proxy_skeleton/skeleton/service_skeleton.h"
namespace ara::sm::triggerout::skeleton {
template <typename T>
class Trigger_Out_Skeleton
    : public ara::com::proxy_skeleton::skeleton::ServiceSkeleton {
public:
  explicit Trigger_Out_Skeleton(
      ara::com::InstanceIdentifier instance,
      ara::com::proxy_skeleton::skeleton::ServiceSkeleton::SK_Handle
          skeleton_handle)
      : ara::com::proxy_skeleton::skeleton::ServiceSkeleton(serviceid, instance,
                                                            skeleton_handle),
        Notifier(this, "Notifier", 0) {}
  ~Trigger_Out_Skeleton() = default;

  // Fields
  typename ara::com::proxy_skeleton::skeleton::FieldType<T, true, true,
                                                         true>::type Notifier;

  void field_method_dispatch(ara::com::SOMEIP_MESSAGE::Message const &message,
                             Socket &cserver) {

    int event_id = message.MessageId().method_id & 0x7FFF;
    switch (event_id) {
    case 2:
      if (message.Length() > 16) {
        Notifier.HandleGet(message, cserver);
      } else {
        Notifier.HandleSet(message, cserver);
      }
      break;
    default:

      break;
    }
  }

private:
  ara::com::proxy_skeleton::ServiceId serviceid{47};
};
} // namespace ara::sm::triggerout::skeleton
