#include <gtest/gtest.h>
#include "TestHelpers.h"

#include "DjinnCompiler.h"
#include "verification/IR.h"

//
// Phase 2 of the verification pass (VERIFICATION-SPEC.md §6.2/§6.3, without
// SMT): claim/inference diffing over the decidable linear fragment.
// `ensure` clauses are checked against inferred return paths (9502 on
// contradiction, `proven` when every path establishes them) and `require`
// clauses are decided at call sites over translatable arguments (9501 on
// provable violation). Undecided checks stay runtime (`checked`).
//

namespace
{
    const char* AFFINE_BODY_SOURCE = R"(
        i32 double_it(i32 x)
            require(x > 0)
            ensure(return == x + x)
        {
            return x * 2;
        }

        i32 main() {
            return double_it(21);
        }
    )";

    const char* CONTRADICTED_ENSURE_SOURCE = R"(
        i32 broken(i32 x)
            require(x > 0)
            ensure(return > 0)
        {
            return 0 - x;
        }

        i32 main() {
            return broken(1);
        }
    )";

    const char* CALL_SITE_VIOLATION_SOURCE = R"(
        i32 debit(i32 balance, i32 amount)
            require(amount > 0)
        {
            return balance - amount;
        }

        i32 main() {
            return debit(100, -50);
        }
    )";

    const char* CALL_SITE_UNKNOWN_SOURCE = R"(
        i32 debit(i32 balance, i32 amount)
            require(amount > 0)
        {
            return balance - amount;
        }

        i32 read_amount() {
            i32 mut x = 1;
            while (x < 10) {
                x = x + 1;
            }
            return x;
        }

        i32 main() throws {
            return debit(100, read_amount());
        }
    )";

    const char* BRANCH_JOIN_SOURCE = R"(
        i32 nonneg(i32 x)
            ensure(return >= 0)
        {
            if (x > 0) {
                return x;
            }
            return 0 - x;
        }

        i32 main() throws {
            return nonneg(7);
        }
    )";

    const char* LOOP_OPACITY_SOURCE = R"(
        i32 loop_sum(i32 n)
            require(n >= 0)
            ensure(return >= 0)
        {
            i32 mut total = 0;
            i32 mut i = 0;
            while (i < n) {
                total = total + i;
                i = i + 1;
            }
            return total;
        }

        i32 main() {
            return loop_sum(4);
        }
    )";

    const djinn::verification::Obligation* find_obligation(const CompilerResult& result,
                                                           const std::string& subject,
                                                           const std::string& description)
    {
        for (const auto& obligation : result.verification.obligations)
        {
            if (obligation.subject == subject && obligation.description == description)
                return &obligation;
        }
        return nullptr;
    }

    CompilerOptions report_options()
    {
        CompilerOptions options;
        options.generateBinary = true;
        options.verificationMode = VerificationMode::Report;
        return options;
    }
}

TEST(VerificationPhase2, EnsureProvenWhenBodyMatchesAffinely)
{
    const auto result = DjinnCompiler::run(AFFINE_BODY_SOURCE, report_options());
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, DJINN_EXIT(42));

    // require stays a runtime check; ensure is discharged statically.
    const auto* require = find_obligation(result, "double_it", "require");
    ASSERT_NE(require, nullptr);
    EXPECT_EQ(require->status, djinn::verification::ObligationStatus::Checked);

    const auto* ensure = find_obligation(result, "double_it", "ensure");
    ASSERT_NE(ensure, nullptr);
    EXPECT_EQ(ensure->status, djinn::verification::ObligationStatus::Proven);
    EXPECT_NE(ensure->justification.find("x + x"), std::string::npos);
}

TEST(VerificationPhase2, EnsureContradictedByBodyIsHardError)
{
    const auto result = DjinnCompiler::run(CONTRADICTED_ENSURE_SOURCE, report_options());

    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH));
    EXPECT_EQ(result.returnCode, 1);

    // The failing function's ensure keeps its runtime check.
    const auto* ensure = find_obligation(result, "broken", "ensure");
    ASSERT_NE(ensure, nullptr);
    EXPECT_EQ(ensure->status, djinn::verification::ObligationStatus::Checked);
}

TEST(VerificationPhase2, CallSiteConstantViolationIsHardError)
{
    const auto result = DjinnCompiler::run(CALL_SITE_VIOLATION_SOURCE, report_options());

    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_PRECONDITION_NOT_ESTABLISHED));
    EXPECT_EQ(result.returnCode, 1);

    bool has_evidence = false;
    for (const auto& diagnostic : result.diagnostics)
    {
        for (const auto& note : diagnostic.notes)
        {
            if (note.find("amount = -50") != std::string::npos) has_evidence = true;
        }
    }
    EXPECT_TRUE(has_evidence);
}

TEST(VerificationPhase2, CallSiteUndecidableArgumentStaysRuntime)
{
    // read_amount()'s result is opaque to the fragment, so the require is
    // not decided at compile time and the program compiles and runs.
    const auto result = DjinnCompiler::run(CALL_SITE_UNKNOWN_SOURCE, report_options());

    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, DJINN_EXIT(90));

    const auto* require = find_obligation(result, "debit", "require");
    ASSERT_NE(require, nullptr);
    EXPECT_EQ(require->status, djinn::verification::ObligationStatus::Checked);
}

TEST(VerificationPhase2, EnsureProvenAcrossBranchJoin)
{
    // Both branches establish return >= 0 (x > 0 and its complement), so the
    // joined postcondition is provable.
    const auto result = DjinnCompiler::run(BRANCH_JOIN_SOURCE, report_options());

    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, DJINN_EXIT(7));

    const auto* ensure = find_obligation(result, "nonneg", "ensure");
    ASSERT_NE(ensure, nullptr);
    EXPECT_EQ(ensure->status, djinn::verification::ObligationStatus::Proven);
}

TEST(VerificationPhase2, LoopAssignedValueKeepsEnsureChecked)
{
    // `total` is assigned inside the loop; the verifier treats it as Unknown
    // and leaves the runtime check in place instead of guessing.
    const auto result = DjinnCompiler::run(LOOP_OPACITY_SOURCE, report_options());

    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, DJINN_EXIT(6));

    const auto* ensure = find_obligation(result, "loop_sum", "ensure");
    ASSERT_NE(ensure, nullptr);
    EXPECT_EQ(ensure->status, djinn::verification::ObligationStatus::Checked);
}

TEST(VerificationPhase2, OffModeSkipsAllChecking)
{
    // Uses a source whose only checking is the verification pass itself —
    // ensure-only contracts, no call-site compile-time overlap — so a zero
    // diagnostic run is attributable to the mode.
    CompilerOptions options;
    options.generateBinary = false;
    options.verificationMode = VerificationMode::Off;

    const auto result = DjinnCompiler::run(BRANCH_JOIN_SOURCE, options);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.verification.obligations.size(), 0);
}
