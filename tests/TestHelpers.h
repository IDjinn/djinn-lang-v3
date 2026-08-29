//
// Created by ZCode on 20/08/2026.
//

#ifndef DJINN_TEST_HELPERS_H
#define DJINN_TEST_HELPERS_H

#include "DjinnCompiler.h"

// Process exit codes: POSIX conveys only the low 8 bits, Windows returns the full value.
// Tests assert the value the djinn program returned, so mask the expectation on POSIX.
#ifdef _WIN32
#define DJINN_EXIT(code) (code)
#else
#define DJINN_EXIT(code) ((code) & 0xFF)
#endif

inline size_t errorCount(const CompilerResult& result)
{
    size_t count = 0;
    for (const auto& diagnostic : result.diagnostics)
        if (diagnostic.severity == Severity::Error) count++;
    return count;
}

inline size_t warningCount(const CompilerResult& result)
{
    size_t count = 0;
    for (const auto& diagnostic : result.diagnostics)
        if (diagnostic.severity == Severity::Warning) count++;
    return count;
}

inline bool hasErrorCode(const CompilerResult& result, const uint32_t code)
{
    for (const auto& diagnostic : result.diagnostics)
        if (diagnostic.code == code) return true;
    return false;
}

#endif //DJINN_TEST_HELPERS_H
