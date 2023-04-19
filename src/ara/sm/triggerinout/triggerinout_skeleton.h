/**
 * @file triggerinout_skeleton.hpp
 * @author Flashing Adapter Graduation Project Team
 * @version 0.1
 * @date 2022-03-14
 *
 * @copyright Copyright (c) 2022
 *
 */

#include "ara/com/proxy_skeleton/definitions.h"
#include "ara/com/proxy_skeleton/skeleton/field.h"
#include "ara/com/proxy_skeleton/skeleton/service_skeleton.h"
namespace ara::sm::triggerinout::proxy_skeleton::skeleton {

template <typename T>
class Trigger_InOut_Skeleton
    : public ara::com::proxy_skeleton::skeleton::ServiceSkeleton {
public:
  explicit Trigger_InOut_Skeleton(
      ara::com::InstanceIdentifier instance,
      ara::com::proxy_skeleton::skeleton::ServiceSkeleton::SK_Handle
          skeleton_handle)
      : ara::com::proxy_skeleton::skeleton::ServiceSkeleton(serviceid, instance,
                                                            skeleton_handle),
        Notifier(this, "Notifier", 0), Trigger(this, "Trigger", 1) {}
  ~Trigger_InOut_Skeleton() = default;
  typename ara::com::proxy_skeleton::skeleton::FieldType<T, true, true, true>::type Notifier;
  typename ara::com::proxy_skeleton::skeleton::FieldType<T, true, true, true>::type Trigger;

  void field_method_dispatch(ara::com::SOMEIP_MESSAGE::Message const &message,
                             Socket &cserver) {

    int event_id = message.MessageId().method_id & 0x7FFF;
    switch (event_id) {
    case 0:
      if (message.Length() > 16) {
        Notifier.HandleGet(message, cserver);
      } else {
        Notifier.HandleSet(message, cserver);
      }
      break;
    case 1:
      if (message.Length() > 16) {
        Trigger.HandleGet(message, cserver);
      } else {
        Trigger.HandleSet(message, cserver);
      }
      break;
    default:
      break;
    }
  }

private:
  ara::com::proxy_skeleton::ServiceId serviceid{49};
};
}