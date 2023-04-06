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
#include <functional>
#include <iostream>
#include <memory>
#include <optional>

namespace ara::com {
class Marshal {
public:
    template <typename... Args>
    std::tuple<typename std::decay_t<Args>...> get_args(const std::vector<uint8_t>& data) const {
        if constexpr (sizeof...(Args) == 0) {
            return std::make_tuple();
        } else {
            std::tuple<typename std::decay_t<Args>...> args;
            const uint8_t* p = data.data();
            std::apply([&p](auto&... a) {((p = unpack(p, a)), ...);}, args);
            return args;
        }
    }

    template <typename Result>
    std::vector<uint8_t> serialize_param(const Result& result) const {
        auto p = (const uint8_t*)(&result);
        return std::vector<uint8_t>(p, p + sizeof(Result));
    }
    
    template <typename... Args>
    std::vector<uint8_t> serialize_params(const Args&... args) const {
        std::vector<uint8_t> payload;
        payload.resize((sizeof(Args) + ...));
        uint8_t* p = payload.data();
        // use fold expression
        (..., (p = pack(p, args)));
        return payload;
    }


private:
    template<typename T>
    static uint8_t* pack(uint8_t *p, const T& val) {
        memcpy(p, &val, sizeof(T));
        return p + sizeof(T);
    }

    template<typename T>
    static const uint8_t* unpack(const uint8_t *p, T& val) {
        memcpy(&val, p, sizeof(T));
        return p + sizeof(T);
    }
};


} // namespace ara::com

#endif // ARA_COM_MARSHAL_H_
