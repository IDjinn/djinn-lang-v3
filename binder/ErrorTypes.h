//
// Builtin error types (core language exceptions)
//
// Errors are a core language feature: the root `Exception` type and common
// derived errors are always available, even without importing the std lib.
// User structs can derive from any error type via `struct MyError : Base;`.
//

#ifndef DJINN_ERROR_TYPES_H
#define DJINN_ERROR_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace djinn::errors
{
    struct BuiltinError
    {
        const char* name;
        const char* base; // empty for the root Exception
        int32_t tag;
    };

    // Tags 1..99 are reserved for builtin errors; user-defined errors start at 100
    inline constexpr int32_t FirstUserErrorTag = 100;

    inline const std::vector<BuiltinError>& builtin_errors()
    {
        static const std::vector<BuiltinError> errors = {
            {"Exception",         "",          1},
            {"Generic",           "Exception", 2},
            {"DivisionByZero",    "Exception", 3},
            {"Argument",          "Exception", 4},
            {"Overflow",          "Exception", 5},
            {"OutOfBounds",       "Exception", 6},
            {"InvalidArgument",   "Exception", 7},
            {"ContractViolation", "Exception", 8},
        };
        return errors;
    }

    inline bool is_builtin_error(const std::string& name)
    {
        for (const auto& e : builtin_errors())
            if (name == e.name) return true;
        return false;
    }

    inline int32_t builtin_error_tag(const std::string& name)
    {
        for (const auto& e : builtin_errors())
            if (name == e.name) return e.tag;
        return -1;
    }
} // namespace djinn::errors

#endif // DJINN_ERROR_TYPES_H
