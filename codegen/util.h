//
// Created by Luke on 26/12/2025.
//

#ifndef DJINN_UTIL_H
#define DJINN_UTIL_H

#include <string>
#include "llvm/Support/raw_ostream.h"

namespace llvm {
    template<typename T>
    std::string to_string(const T &obj) {
        std::string str;
        raw_string_ostream stream(str);
        obj.print(stream);
        return str;
    }

    template<typename T>
    std::string to_string(const T *obj) {
        assert(obj != nullptr);
        return to_string(*obj);
    }
}

#endif //DJINN_UTIL_H
