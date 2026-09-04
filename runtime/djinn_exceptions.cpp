//
// Native-exceptions shim (linked only with --exceptions). The compiler emits
// plain calls to these entry points; throwing/catching actual C++ exceptions
// here lets the platform ABI (SEH on Windows, Itanium __cxa elsewhere) carry
// djinn errors through any frames — including C++ ones — with zero-cost
// tables on the happy path.
//
// A single C++ type crosses the boundary: djinn::error. Fine-grained djinn
// error-type matching (the Exception hierarchy) happens inside the IR catch
// handlers by tag; C++ code may also catch djinn::error or std::exception
// directly.
//

#include "djinn_error.h"

#include "djinn_runtime.h"

#include <exception>

namespace djinn
{
    // Foreign C++ exceptions caught by djinn handlers are wrapped so the
    // uniform {tag, message, type_name} view keeps working. One thread-local
    // slot suffices — a single foreign wrap is in flight per dispatch chain
    // at any time — keeping the path allocation-free.
    static DJINN_TLS error foreign_error_slot;

    static error* wrap_foreign_exception()
    {
        error& wrapped = foreign_error_slot;
        wrapped.tag = DJINN_FOREIGN_ERROR_TAG;
        wrapped.message = nullptr;
        wrapped.type_name = "ForeignError";
        try
        {
            throw;
        }
        catch (const std::exception& ex)
        {
            wrapped.message = ex.what();
        }
        catch (...)
        {
            wrapped.message = "foreign C++ exception";
        }
        __djinn_capture_backtrace();

        // Mirror into the thread-local error state — native handlers re-read
        // it after catching, exactly like __djinn_throw does
        __djinn_errno.flag = 1;
        __djinn_errno.tag = wrapped.tag;
        __djinn_errno.message = wrapped.message;
        __djinn_errno.type_name = wrapped.type_name;
        __djinn_errno.origin_file = nullptr;
        __djinn_errno.origin_line = 0;
        __djinn_errno.origin_column = 0;
        return &wrapped;
    }
}

const char* djinn::error::what() const noexcept
{
    return message != nullptr ? message : (type_name != nullptr ? type_name : "djinn error");
}

extern "C" {
// The single throw entry point the generator emits: mirrors the error
// into the thread-local state, captures the raise-site trace into the
// thread-local trace storage and throws a token-sized object by value
// (the platform runtime copies it into its own exception storage — the
// only allocation on the throw path, inherent to the standard C++ ABI).
[[noreturn]] void __djinn_throw(const int32_t tag, const char* const message,
                                const char* const type_name)
{
    __djinn_capture_backtrace();

    __djinn_errno.flag = 1;
    __djinn_errno.tag = tag;
    __djinn_errno.message = message;
    __djinn_errno.type_name = type_name;
    // origin stays whatever the raising site recorded (the generator
    // stores it before calling here)

    throw djinn::error{tag, message, type_name};
}

// Rethrow from inside a handler (the generator's unmatched-arm path).
[[noreturn]] void __djinn_rethrow()
{
    throw;
}

// Wrap the in-flight foreign exception as a ForeignError djinn::error.
djinn::error* __djinn_wrap_foreign()
{
    return djinn::wrap_foreign_exception();
}

// Renders the uncaught-exception report (fancy report + trace) for an
// error that escaped user main, then aborts (never returns).
[[noreturn]] int32_t __djinn_report_uncaught(const djinn::error* err)
{
    __djinn_uncaught_error(err->tag, err->type_name, err->message, nullptr, 0, 0);
    __builtin_unreachable();
}
}
