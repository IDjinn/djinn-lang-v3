//
// Error-flow enforcement levels (contract-style), selected via
// CompilerOptions::errorEnforcement.
//

#ifndef DJINN_ERROR_ENFORCEMENT_H
#define DJINN_ERROR_ENFORCEMENT_H

enum class ErrorEnforcement
{
    // No error-flow checks: no MISSING_TRY, no compile-time analysis.
    Off,
    // Runtime semantics only: binder enforces `try` in non-throwing callers;
    // unchecked calls inside throwing functions propagate.
    Runtime,
    // Runtime + provable violations at call sites become compile errors
    // (constexpr calls that always throw, require clauses violated by
    // constant arguments); handled-but-provable calls warn.
    CompileTime,
    // CompileTime + every call to a throwing function must be wrapped in
    // `try` (a bare `try` propagates), even inside throwing functions.
    Strict
};

#endif //DJINN_ERROR_ENFORCEMENT_H
