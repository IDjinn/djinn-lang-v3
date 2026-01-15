//
// Created by Claude on 06/01/2026.
//

#ifndef DJINN_INTRINSICS_H
#define DJINN_INTRINSICS_H

#include <string>
#include <optional>
#include <unordered_map>

enum class Intrinsic {
    Sizeof,
    Alignof,
    Bitcast,
    Trap,
    Unreachable,
    Expect,
    Likely,
    Unlikely,
    DebugTrap
};

inline std::optional<Intrinsic> get_intrinsic(const std::string &name) {
    static const std::unordered_map<std::string, Intrinsic> intrinsics = {
        {"sizeof", Intrinsic::Sizeof},
        {"alignof", Intrinsic::Alignof},
        {"bitcast", Intrinsic::Bitcast},
        {"trap", Intrinsic::Trap},
        {"debugtrap", Intrinsic::DebugTrap},
        {"unreachable", Intrinsic::Unreachable},
        {"expect", Intrinsic::Expect},
        {"likely", Intrinsic::Likely},
        {"unlikely", Intrinsic::Unlikely}
    };

    if (const auto it = intrinsics.find(name); it != intrinsics.end()) {
        return it->second;
    }
    return std::nullopt;
}

inline bool is_intrinsic(const std::string &name) {
    return get_intrinsic(name).has_value();
}

#endif //DJINN_INTRINSICS_H