#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>
#include <tuple>
#include <cstring>

using namespace std;

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

float test1(int a, float b) {
    return a + b;
}

int main() {
    Marshal m;
    auto payload = m.serialize_params(1, 2.2f);

    auto args = m.get_args<int, float>(payload);

    auto result = std::apply(test1, args);
    std::cout << result << std::endl;
    return 0;
}