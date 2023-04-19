/**
 * @file marshal.hpp
 * @author Flashing Adapter Graduation Project Team
 * @version 0.1
 * @date 2022-02-25
 *
 * @copyright Copyright (c) 2022
 *
 */

#ifndef ARA_COM_MARSHAL_H_
#define ARA_COM_MARSHAL_H_

#include "ara/com/deserializer.h"
#include "serializer.h"
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <tuple>

namespace ara::com {
class Marshal {
public:
  template <typename... Args>
  std::tuple<typename std::decay_t<Args>...>
  get_args(const std::vector<uint8_t> &data) const {
    if constexpr (sizeof...(Args) == 0) {
      return std::make_tuple();
    } else {
      auto index = 0;
      std::tuple<typename std::decay_t<Args>...> result;
      Deserializer deserializer;
      std::apply(
          [&data, &index, &deserializer](auto &...args) {
            ((args = deserializer.deserialize<std::decay_t<decltype(args)>>(data, index)),
             ...);
          },
          result);
      return result;
    }
  }

  template <typename... Args>
  std::vector<uint8_t> serialize_params(const Args &...args) const {
    Serializer serializer;
    (serializer.serialize(args), ...);
    return serializer.Payload();
  }
};

} // namespace ara::com

#endif // ARA_COM_MARSHAL_H_
