/**
 * @file serializer.hpp
 * @author Flashing Adapter Graduation Project Team
 * @version 0.1
 * @date 2022-02-25
 *
 * @copyright Copyright (c) 2022
 *
 */

#ifndef ARA_COM_SERIALIZER_H_
#define ARA_COM_SERIALIZER_H_

#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ara::com {
/**
 * @brief Serializer class for serializing data
 *
 */
class Serializer {
private:
  std::vector<uint8_t> payload; //!< The payload of the serializer

public:
  /**
   * @brief Construct a new Serializer object
   *
   */
  Serializer() = default;
  /**
   * @brief Destroy the Serializer object
   *
   */
  ~Serializer() = default;
  /**
   * @brief serialize the data into the vector of bytes
   *
   * @tparam T
   * @param data
   */
  template <typename T> void serialize(T data) {
    // if T is string or vector<uint8_t> 
    if constexpr (std::is_same<T, std::string>::value ||
                  std::is_same<T, std::vector<uint8_t>>::value) {
      payload.insert(payload.end(), data.begin(), data.end());

    }else{
    std::vector<uint8_t> buffer(sizeof(data));
    memcpy(&buffer[0], (const void *)&data, sizeof(data));
    payload.insert(payload.end(), buffer.begin(), buffer.end());
    }
  }
  /**
   * @brief Clear the payload of the serializer
   *
   */
  void Clear() { payload.clear(); }
  /**
   * @brief Get the Payload object of the serializer
   *
   */
  std::vector<uint8_t> Payload() { return payload; }
};
} // namespace ara::com

#endif // ARA_COM_SERIALIZER_H_
