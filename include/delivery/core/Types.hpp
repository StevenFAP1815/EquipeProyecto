#pragma once

#include <cstdint>
#include <functional>
#include <ostream>
#include <string>
#include <utility>

namespace delivery::core {

// ---------------------------------------------------------------------------
// Strong typedefs para IDs del dominio.
//
// Cada ID envuelve un std::string (los IDs del proyecto son cadenas como "n0",
// "s3", "r1", "c5", "o17"). El wrapper impide mezclar tipos por accidente:
// pasar un CourierId donde se espera un OrderId no compila.
//
// La macro DEFINE_ID_TYPE genera, para cada tipo:
//   - constructor explicito desde std::string (y desde const char*)
//   - operator== y operator!= (C++17 no tiene operator<=>)
//   - operator< para usar en std::map / std::set
//   - std::hash especializado para unordered_map / unordered_set
//   - operator<< para logs y mensajes de error
// ---------------------------------------------------------------------------

#define DELIVERY_DEFINE_ID_TYPE(TypeName)                                      \
    struct TypeName {                                                          \
        std::string value;                                                     \
                                                                               \
        TypeName() = default;                                                  \
        explicit TypeName(std::string v) : value(std::move(v)) {}              \
        explicit TypeName(const char* v) : value(v) {}                         \
                                                                               \
        bool operator==(const TypeName& other) const {                         \
            return value == other.value;                                       \
        }                                                                      \
        bool operator!=(const TypeName& other) const {                         \
            return !(*this == other);                                          \
        }                                                                      \
        bool operator<(const TypeName& other) const {                          \
            return value < other.value;                                        \
        }                                                                      \
                                                                               \
        const std::string& str() const { return value; }                       \
        bool empty() const { return value.empty(); }                           \
    };                                                                         \
                                                                               \
    inline std::ostream& operator<<(std::ostream& os, const TypeName& id) {    \
        return os << id.value;                                                 \
    }                                                                          \
                                                                               \
    struct TypeName##Hash {                                                    \
        std::size_t operator()(const TypeName& id) const {                     \
            return std::hash<std::string>{}(id.value);                         \
        }                                                                      \
    };

DELIVERY_DEFINE_ID_TYPE(NodeId)
DELIVERY_DEFINE_ID_TYPE(StreetId)
DELIVERY_DEFINE_ID_TYPE(RestaurantId)
DELIVERY_DEFINE_ID_TYPE(CourierId)
DELIVERY_DEFINE_ID_TYPE(OrderId)

#undef DELIVERY_DEFINE_ID_TYPE

}  // namespace delivery::core