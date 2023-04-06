/**
 * @file diagnostic_reset_skeleton.hpp
 * @author Flashing Adapter Graduation Project Team
 * @brief
 * @version 0.1
 * @date 2022-03-14
 *
 * @copyright Copyright (c) 2022
 *
 */

#include "ara/com/proxy_skeleton/skeleton/service_skeleton.h"
#include "ara/com/proxy_skeleton/skeleton/field.h"

#include "ara/sm/diagnostic_reset/diagnostic_reset_return_types.h"
#include <iostream>
#include <future>

namespace ara::sm::diagnostic_reset::skeleton {
class diagnostic_reset_skeleton : public ara::com::proxy_skeleton::skeleton::ServiceSkeleton {
private:
  struct sockaddr_in cliaddr;
  ara::com::proxy_skeleton::ServiceId serviceid{14};

public:
  diagnostic_reset_skeleton(
      ara::com::InstanceIdentifier instance,
      ara::com::proxy_skeleton::skeleton::ServiceSkeleton::SK_Handle
          skeleton_handle)
      : ara::com::proxy_skeleton::skeleton::ServiceSkeleton(serviceid, instance,
                                                            skeleton_handle) {}

  /*sends DiagnosticResetMsg defined in 9.1 Type definition to all Processes in
   * a SoftwareCluster.*/
  std::future<ara::sm::diagnostic_reset::MessageDiagnosticOutput> message();

  /*All Processes which got a DiagnosticReset request sends this as answer to
   * State Management*/
  std::future<ara::sm::diagnostic_reset::EventDiagnosticOutput> event();

  void method_dispatch(ara::com::SOMEIP_MESSAGE::Message &message,
                                 Socket &cserver) {
    int methodID = message.MessageId().method_id;
    // std::cout << "\t[SERVER] Dispatch " << methodID << endl;

    switch (methodID) {
    case 0:
      HandleCall(*this, &diagnostic_reset_skeleton::message, message, cserver);
      break;
    case 1:
      HandleCall(*this, &diagnostic_reset_skeleton::event, message, cserver);
      break;
    default:


      break;
    }
  }
};
} // namespace ara::sm::diagnostic_reset
