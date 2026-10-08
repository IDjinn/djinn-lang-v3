#include <gtest/gtest.h>
#include "TestHelpers.h"

#include "DjinnCompiler.h"
#include "verification/IR.h"

//
// Interprocedural verification (VERIFICATION-SPEC.md §6.2/§12.3): proven
// postconditions propagate through assignments so call chains are verified
// across function boundaries, method call sites are checked against their
// contracts, and contract-bearing extern stubs are verified spec-first.
// `main() throws` lets contract-bearing callees propagate instead of
// demanding a `try` wrapper, keeping verification the only diagnostic source.
//

namespace
{
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

    CompilerOptions report_options(const bool generate_binary = true)
    {
        CompilerOptions options;
        options.generateBinary = generate_binary;
        options.verificationMode = VerificationMode::Report;
        return options;
    }
}

TEST(VerificationInterproc, ChainedPostconditionProvesCallerEnsure)
{
    // quad's ensure is only decidable through the postcondition propagated
    // from double_it across two call assignments.
    const char* source = R"(
        i32 double_it(i32 x)
            ensure(return == x + x)
        {
            return x * 2;
        }

        i32 quad(i32 x)
            ensure(return == x + x + x + x)
        {
            i32 a = double_it(x);
            i32 b = double_it(a);
            return a + b;
        }

        i32 main() throws {
            return quad(3);
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, DJINN_EXIT(12));

    const auto* ensure = find_obligation(result, "quad", "ensure");
    ASSERT_NE(ensure, nullptr);
    EXPECT_EQ(ensure->status, djinn::verification::ObligationStatus::Proven);
}

TEST(VerificationInterproc, PropagatedValueFeedsRequireChecking)
{
    // y == -6 is only known via double_it's propagated postcondition; the
    // verifier rejects needs_pos(y) at compile time with the evidence.
    const char* source = R"(
        i32 double_it(i32 x)
            ensure(return == x + x)
        {
            return x * 2;
        }

        i32 needs_pos(i32 v)
            require(v > 0)
        {
            return v;
        }

        i32 main() throws {
            i32 y = 0 - double_it(3);
            return needs_pos(y);
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_PRECONDITION_NOT_ESTABLISHED));
    EXPECT_EQ(result.returnCode, 1);

    bool has_evidence = false;
    for (const auto& diagnostic : result.diagnostics)
    {
        for (const auto& note : diagnostic.notes)
        {
            if (note.find("v = -6") != std::string::npos) has_evidence = true;
        }
    }
    EXPECT_TRUE(has_evidence);
}

TEST(VerificationInterproc, MethodCallSiteCheckedAgainstContracts)
{
    const char* source = R"(
        struct Acc {
            i32 balance;
        }

        impl Acc {
            i32 withdraw(i32 amount)
                require(amount > 0)
            {
                this.balance = this.balance - amount;
                return this.balance;
            }
        }

        i32 main() throws {
            Acc a = { 100 };
            return a.withdraw(-5);
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_PRECONDITION_NOT_ESTABLISHED));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationInterproc, MethodCallSiteSatisfiedPasses)
{
    const char* source = R"(
        struct Acc {
            i32 balance;
        }

        impl Acc {
            i32 withdraw(i32 amount)
                require(amount > 0)
            {
                this.balance = this.balance - amount;
                return this.balance;
            }
        }

        i32 main() throws {
            Acc a = { 100 };
            return a.withdraw(40);
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, DJINN_EXIT(60));
}

TEST(VerificationInterproc, ExternStubRequireCheckedAtCallSite)
{
    // Spec-first stub (§12.3): the contract exists before the body does.
    const char* source = R"(
        extern fn charge(i32 amount) -> i32
            require(amount > 0);

        i32 main() {
            return charge(-5);
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options(false));
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_PRECONDITION_NOT_ESTABLISHED));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationInterproc, RecursiveCallsTerminateAndStayChecked)
{
    // The recursive path returns an opaque value, so the ensure is not
    // provable — it stays a runtime check and the analysis terminates.
    const char* source = R"(
        i32 count_down(i32 n)
            ensure(return >= 0)
        {
            if (n <= 0) {
                return 0;
            }
            return count_down(n - 1) + 1;
        }

        i32 main() throws {
            return count_down(3);
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, DJINN_EXIT(3));

    const auto* ensure = find_obligation(result, "count_down", "ensure");
    ASSERT_NE(ensure, nullptr);
    EXPECT_EQ(ensure->status, djinn::verification::ObligationStatus::Checked);
}
