/**
 * @file deserializer.hpp
 * @author Flashing Adapter Graduation Project Team
 * @version 0.1
 * @date 2022-02-25
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef ARA_COM_DESERIALIZER_H_
#define ARA_COM_DESERIALIZER_H_

#include <cstdint>
#include <map>
#include <string.h>
#include <string>
#include <vector>
#include <iostream>

namespace ara::com {
/**
 * @brief Deserializer class for deserializing data
 */
class Deserializer {
public:
  /**
   * @brief Construct a new Deserializer object
   *
   */
  Deserializer() = default;
  /**
   * @brief Destroy the Deserializer object
   *
   */
  ~Deserializer() = default;
  /**
   * @brief Deserialize the vector of bytes into the required data type
   *
   * @tparam T    Data type to be deserialized
   * @param data  Vector of bytes to be deserialized
   * @param ind   Index of the vector of bytes to be deserialized
   * @return T    Data tyoe of Deserialized and returned data
   */
  template <typename T> T deserialize(std::vector<uint8_t> data, int &ind) {
    // if T is vector<uint8_t> or string
    if constexpr (std::is_same_v<T, std::vector<uint8_t>> ||
                  std::is_same_v<T, std::string>) {

      // get the size of the data
      int size  = 0 ;
      memcpy (&size, (const void *)&data[ind], sizeof(size));
      ind += sizeof(size);
      T result(size); // Variable to store the deserialized data
      memcpy(&result[0], (const void *)&data[ind],
             size); // Copy the data from the vector of bytes to the variable of
                    // the required data type
       ind += size;
      return result; // Return the deserialized data

    }
    // if T is vector of string or vector of vector of uint8_t
    else if constexpr (std::is_same_v<T, std::vector<std::string>> ||
                       std::is_same_v<T, std::vector<std::vector<uint8_t>>>) {
      // get the size of the data
      int size  = 0 ;
      memcpy (&size, (const void *)&data[ind], sizeof(size));
      ind += sizeof(size);
      T result(size); // Variable to store the deserialized data
      // for each element in the vector
      for (int i = 0 ; i < size; i++) {
        // get the size of the element
        int size2  = 0 ;
        memcpy (&size2, (const void *)&data[ind], sizeof(size2));
        ind += sizeof(size2);
        result[i].resize(size2); // resize the element
        memcpy(&result[i][0], (const void *)&data[ind],
               size2); // Copy the data from the vector of bytes to the variable
                       // of the required data type
        ind += size2;
      }
      return result; // Return the deserialized data
    }
    T result; // Variable to store the deserialized data
    memcpy(&result, (const void *)&data[ind],
           sizeof(result)); // Copy the data from the vector of bytes to the
                            // variable of the required data type
    ind += sizeof(result);
    return result; // Return the deserialized data
  }
};

} // namespace ara::com

#endif // ARA_COM_DESERIALIZER_H_
