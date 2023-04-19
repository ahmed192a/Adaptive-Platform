/**
 * @file powermode_common.hpp
 * @author Flashing Adapter Graduation Project Team
 * @brief
 * @version 0.1
 * @date 2022-03-14
 *
 * @copyright Copyright (c) 2022
 *
 */

#ifndef COMMON_H_
#define COMMON_H_

#include "ara/com/proxy_skeleton/proxy/service_proxy.h"
#include "powermode_return_types.h"
#include "string"

namespace ara::sm::powermode::proxy::methods {
class MessagePowerMode {
private:
  const std::string methodName_ = "MessagePowerMode";
  ara::com::proxy_skeleton::proxy::ServiceProxy *service_proxy;
  const int methodid = 0;

public:
  explicit MessagePowerMode(
      ara::com::proxy_skeleton::proxy::ServiceProxy *service_proxy)
      : service_proxy(service_proxy) {}

  std::string getMethodName() const { return methodName_; }

  std::future<ara::sm::powermode::MessagePowermodeOutput> operator()() {
    std::future<ara::sm::powermode::MessagePowermodeOutput> result =
        std::async([this]() {
          // add function code
          return service_proxy
              ->SendRequest<ara::sm::powermode::MessagePowermodeOutput>(
                  methodid);
        });
    return result;
  }
};

class EventPowerMode {
private:
  const std::string methodName_ = "EventPowerMode";
  ara::com::proxy_skeleton::proxy::ServiceProxy *service_proxy;
  const int methodid = 1;

public:
  explicit EventPowerMode(
      ara::com::proxy_skeleton::proxy::ServiceProxy *service_proxy)
      : service_proxy(service_proxy) {}
  std::string getMethodName() const { return methodName_; }
  std::future<ara::sm::powermode::EventPowermodeOutput> operator()() {
    std::future<ara::sm::powermode::EventPowermodeOutput> result =
        std::async([this]() {
          // add function code
          return service_proxy
              ->SendRequest<ara::sm::powermode::EventPowermodeOutput>(methodid);
        });
    return result;
  }
};
} // namespace ara::sm::powermode::proxy::methods

#endif