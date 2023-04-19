/**
 * @file triggerin_skeleton.hpp
 * @author
 * @brief
 * @version 0.1
 * @date 2022-03-15
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef SM_TRIGGERIN_SKELETON_H_
#define SM_TRIGGERIN_SKELETON_H_
#include "ara/com/proxy_skeleton/skeleton/field.h"
#include "ara/com/proxy_skeleton/skeleton/service_skeleton.h"
#include "ara/sm/triggerin/triggerin_types.h"

namespace ara::sm::triggerin::skeleton {
namespace fields {
using UCM_Trigger =
    ara::com::proxy_skeleton::skeleton::FieldType<ara::sm::triggerin::UCM_State,
                                                  true, true, true>::type;
using OTA_Trigger =
    ara::com::proxy_skeleton::skeleton::FieldType<ara::sm::triggerin::OTA_State,
                                                  true, true, true>::type;
} // namespace fields

template <typename T>
class Trigger_In_skeleton
    : public ara::com::proxy_skeleton::skeleton::ServiceSkeleton {
  ara::com::proxy_skeleton::ServiceId m_serviceid{0};

public:
  explicit Trigger_In_skeleton(
      ara::com::proxy_skeleton::ServiceId serviceid,
      ara::com::InstanceIdentifier instance,
      ara::com::proxy_skeleton::skeleton::ServiceSkeleton::SK_Handle
          skeleton_handle)
      : ara::com::proxy_skeleton::skeleton::ServiceSkeleton(
            m_serviceid, instance, skeleton_handle),
        m_serviceid(serviceid), trigger(this, "trigger", 0) {}

  // Fields
  typename ara::com::proxy_skeleton::skeleton::FieldType<T, true, true,
                                                         true>::type trigger;

  void field_method_dispatch(ara::com::SOMEIP_MESSAGE::Message const &message,
                             Socket &cserver) {

    int event_id = message.MessageId().method_id & 0x7FFF;
    if (event_id == 0) {
      if (message.Length() > 16) {
        trigger.HandleSet(message, cserver);
      } else {
        trigger.HandleGet(message, cserver);
      }

    } else {
      NoMethodHandler(message.MessageId().method_id, cserver);
    }
  }
};

} // namespace ara::sm::triggerin::skeleton
#endif // SM_TRIGGERIN_SKELETON_H_