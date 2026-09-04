//
// djinn::error — the single C++ type that crosses the djinn/LLVM-EH boundary
// (see djinn_exceptions.cpp). Deliberately tiny: it is thrown by value and
// the platform EH runtime copies it into its exception storage, so the
// payload lives in thread-local state instead — the raise-site trace in
// djinn_error_trace, everything else in __djinn_errno (zero-overhead-
// exception style: the exception object is just a token). Catch handlers in
// generated code re-read that state; C++ code linked against a djinn binary
// may catch this (or std::exception) directly.
//

#ifndef DJINN_ERROR_H
#define DJINN_ERROR_H

#include <stdint.h>

#include "djinn_runtime.h"

// Must match binder/ErrorTypes.h (tags 1..99 reserved for builtins)
#define DJINN_FOREIGN_ERROR_TAG 9

#ifdef __cplusplus

#include <exception>

namespace djinn
{
    struct error : std::exception
    {
        int32_t tag = 0;
        const char* message = nullptr;
        const char* type_name = nullptr;

        const char* what() const noexcept override;
    };
}

extern "C" {
#endif // __cplusplus

// runtime/djinn_exceptions.cpp — only linked with --exceptions. Both throw
// entry points are noreturn; __djinn_report_uncaught renders the uncaught
// report and aborts.
[[noreturn]] void __djinn_throw(int32_t tag, const char* message, const char* type_name);
[[noreturn]] void __djinn_rethrow(void);
#ifdef __cplusplus
djinn::error* __djinn_wrap_foreign(void);
[[noreturn]] int32_t __djinn_report_uncaught(const djinn::error* err);
#endif

#ifdef __cplusplus
}
#endif

#endif // DJINN_ERROR_H
