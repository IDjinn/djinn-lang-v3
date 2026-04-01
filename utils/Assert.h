#ifndef DJINN_ASSERT_H
#define DJINN_ASSERT_H

#include <iostream>
#include <stacktrace>
#include <stdexcept>
#include <string>

struct AssertionError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

#ifdef DJINN_TESTING
#define DJINN_ASSERT(expr, msg) \
    do { \
        if (!(expr)) [[unlikely]] { \
            auto _st = std::stacktrace::current(); \
            std::string _msg = std::string(msg) + "\n"; \
            for (const auto& _entry : _st) { \
                _msg += std::to_string(_entry) + "\n"; \
            } \
            throw AssertionError(_msg); \
        } \
    } while (false)
#else
#define DJINN_ASSERT(expr, msg) \
    do { \
        if (!(expr)) [[unlikely]] { \
            std::cerr << "ASSERT FAILED: " << (msg) << "\n" \
                      << std::stacktrace::current() << std::endl; \
            abort(); \
        } \
    } while (false)
#endif

#endif //DJINN_ASSERT_H