#ifndef DJINN_VERIFIER_MACROS_H
#define DJINN_VERIFIER_MACROS_H

#include "../diagnostics/Diagnostic.h"

#define VERIFIER_ERROR(code, msg, location) do { \
    _diagnostics.emitAndPrint(Diagnostic(Severity::Error, code, msg, location)); \
    throw CompileError(code, msg, location); \
} while (false)

#define VERIFIER_WARNING(code, msg, location) do { \
    _diagnostics.emitAndPrint(Diagnostic(Severity::Warning, code, msg, location)); \
} while (false)

#endif //DJINN_VERIFIER_MACROS_H
