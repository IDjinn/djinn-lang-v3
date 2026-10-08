#include <gtest/gtest.h>
#include "TestHelpers.h"

#include "DjinnCompiler.h"
#include "verification/IR.h"

//
// Semantic contract surface (VERIFICATION-SPEC.md §5.1/§5.2, static only):
// semantic sections are claims diffed against body inference, uow scopes get
// their static checks (atomic coverage, retry safety, effect placement,
// ordering graph, access conflicts), entity invariants are proven at method
// exits, and clause modes prove/assume/ignore select the verification
// contract. Nothing here changes the generated binary.
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

    CompilerOptions report_options(const bool generate_binary = false)
    {
        CompilerOptions options;
        options.generateBinary = generate_binary;
        options.verificationMode = VerificationMode::Report;
        return options;
    }
}

TEST(VerificationSemantics, WritesClaimMatchingBodyPasses)
{
    const char* source = R"(
        struct Box {
            i32 value;
        }

        impl Box {
            i32 bump() writes: self.value {
                this.value = this.value + 1;
                return this.value;
            }
        }

        i32 main() throws {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(VerificationSemantics, WritesClaimMismatchIsHardError)
{
    // The body never writes what the claim declares (spec §6.2: inferred ==
    // declared for writes).
    const char* source = R"(
        struct Box {
            i32 value;
        }

        impl Box {
            i32 bump() writes: self.other {
                this.value = this.value + 1;
                return this.value;
            }
        }

        i32 main() throws {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationSemantics, ReadsClaimHiddenPathIsHardError)
{
    const char* source = R"(
        struct Box {
            i32 value;
            i32 spare;
        }

        impl Box {
            i32 peek_both() reads: self.value {
                return this.value + this.spare;
            }
        }

        i32 main() throws {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationSemantics, EffectsClaimHiddenEffectIsHardError)
{
    const char* source = R"(
        import std::sys;

        i32 noisy()
            effects: memory
        {
            printf("hi\n");
            return 0;
        }

        i32 main() throws {
            return noisy();
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationSemantics, EffectsClaimCoveringBodyPasses)
{
    const char* source = R"(
        import std::sys;

        i32 noisy()
            effects: external_io
        {
            printf("hi\n");
            return 0;
        }

        i32 main() throws {
            return noisy();
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(VerificationUow, AtomicCoverageMismatchIsHardError)
{
    const char* source = R"(
        uow Move {
            atomic: a.value

            body {
                b.value = b.value + 1;
            }
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationUow, RetryAllowedWithExternalIoIsUnsafe)
{
    const char* source = R"(
        import std::sys;

        uow Replicate {
            retry: allowed

            body {
                printf("tick\n");
            }
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_RETRY_UNSAFE_EFFECT));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationUow, ExternalIoInAfterCommitIsSafe)
{
    const char* source = R"(
        import std::sys;

        uow Notify {
            strategy: database
            effects: external_io

            body {
            }

            after_commit {
                printf("done\n");
            }
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(VerificationUow, AccessModeConflictIsHardError)
{
    const char* source = R"(
        uow Dual {
            access: shared ledger, exclusive ledger

            body {
            }
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationUow, OrderingConflictAcrossUowsIsHardError)
{
    const char* source = R"(
        uow A {
            ordering: Account.id ascending

            body {
            }
        }

        uow B {
            ordering: Account.id descending

            body {
            }
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_ORDERING_CYCLE));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationUow, UowEmitsNoCode)
{
    // The uow is a compile-time verification scope: the generated program is
    // identical to one without it.
    const char* source = R"(
        uow Ghost {
            body {
                printf("never executed\n");
            }
        }

        i32 main() {
            return 7;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options(true));
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, DJINN_EXIT(7));
    EXPECT_EQ(result.verification.unitsOfWork.size(), 1);
}

TEST(VerificationInvariants, InvariantProvenOnNaturalWriters)
{
    const char* source = R"(
        struct Acc {
            i32 balance;
            invariant this.balance >= 0 in mode prove
        }

        impl Acc {
            i32 withdraw(i32 amount)
                require(amount > 0)
                require(this.balance >= amount)
            {
                this.balance = this.balance - amount;
                return this.balance;
            }

            i32 deposit(i32 amount)
                require(amount > 0)
            {
                this.balance = this.balance + amount;
                return this.balance;
            }
        }

        i32 main() throws {
            Acc a = { 100 };
            return a.withdraw(40);
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options(true));
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, DJINN_EXIT(60));
}

TEST(VerificationInvariants, InvariantBrokenIsHardError)
{
    const char* source = R"(
        struct Acc {
            i32 balance;
            invariant this.balance >= 0 in mode prove
        }

        impl Acc {
            i32 reset(i32 amount)
                require(amount > 0)
            {
                this.balance = 0 - amount;
                return this.balance;
            }
        }

        i32 main() throws {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_INVARIANT_NOT_PRESERVED));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationInvariants, InvariantUndecidableUnderProveFails)
{
    // The writer's value is opaque, so `prove` cannot discharge the
    // invariant — a hard error forcing an explicit mode (no silent
    // downgrade, spec §3).
    const char* source = R"(
        struct Acc {
            i32 balance;
            invariant this.balance >= 0 in mode prove
        }

        impl Acc {
            i32 set_raw(i32 value)
            {
                this.balance = value;
                return this.balance;
            }
        }

        i32 main() throws {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_UNPROVABLE));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationModes, AssumeSkipsEnsureChecking)
{
    const char* source = R"(
        i32 broken(i32 x)
            ensure(return >= 0) in mode assume
        {
            return 0 - x;
        }

        i32 main() throws {
            return broken(1);
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_EQ(result.diagnostics.size(), 0);

    const auto* ensure = find_obligation(result, "broken", "ensure");
    ASSERT_NE(ensure, nullptr);
    EXPECT_EQ(ensure->status, djinn::verification::ObligationStatus::Assumed);
}

TEST(VerificationModes, ProveUnprovableEnsureIsHardError)
{
    const char* source = R"(
        i32 opaque(i32 x)
            ensure(return >= 0) in mode prove
        {
            i32 mut total = 0;
            i32 mut i = 0;
            while (i < x) {
                total = total + i;
                i = i + 1;
            }
            return total;
        }

        i32 main() throws {
            return opaque(3);
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_UNPROVABLE));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationModes, IgnoreDropsObligation)
{
    const char* source = R"(
        i32 doc_only(i32 x)
            ensure(return >= 0) in mode ignore
        {
            return 0 - x;
        }

        i32 main() throws {
            return doc_only(1);
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(find_obligation(result, "doc_only", "ensure"), nullptr);
}
