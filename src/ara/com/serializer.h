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
    if constexpr (std::is_same_v<T, std::string> ||
                  std::is_same_v<T, std::vector<uint8_t>>) {

      // insert size of the data
      int size = data.size();

      std::vector<uint8_t> buffer(sizeof(size));
      memcpy(&buffer[0], (const void *)&size, sizeof(size));
      payload.insert(payload.end(), buffer.begin(), buffer.end());
      payload.insert(payload.end(), data.begin(), data.end());

    }
    // else if it's vector of strings or vector of vector<uint8_t>
    else if constexpr (std::is_same_v<T, std::vector<std::string>> ||
                       std::is_same_v<T, std::vector<std::vector<uint8_t>>>) {
      // insert size of the data
      int size = data.size();
      std::vector<uint8_t> buffer(sizeof(size));
      memcpy(&buffer[0], (const void *)&size, sizeof(size));
      payload.insert(payload.end(), buffer.begin(), buffer.end());
      payload.insert(payload.end(), data.size());
      for (auto &element : data) {
        // insert size of each element
        int size2 = element.size();
        std::vector<uint8_t> buffer2(sizeof(size2));
        memcpy(&buffer2[0], (const void *)&size2, sizeof(size2));
        payload.insert(payload.end(), buffer2.begin(), buffer2.end());

        payload.insert(payload.end(), element.begin(), element.end());
      }
    } else {
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
  std::vector<uint8_t> Payload() const { return payload; }
};
} // namespace ara::com

#endif // ARA_COM_SERIALIZER_H_
