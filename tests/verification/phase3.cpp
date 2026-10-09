#include <gtest/gtest.h>
#include "TestHelpers.h"

#include "DjinnCompiler.h"
#include "verification/IR.h"

//
// Semantic contract surface (VERIFICATION-SPEC.md §5.1/§5.2, static only):
// semantic sections are claims diffed against body inference, uows are pure
// specifications that real functions attach to with `uow (Name)` and
// get their static checks per member (atomic coverage, retry safety, effect
// placement, ordering graph, access conflicts), entity invariants are proven
// at method exits, and clause modes prove/assume/ignore select the
// verification contract. Nothing here changes the generated binary except
// mode gating: only `in mode check` reaches runtime.
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
            i32 bump() writes(self.value) {
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
            i32 bump() writes(self.other) {
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
            i32 peek_both() reads(self.value) {
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
            effects(memory)
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
            effects(external_io)
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
    // The member writes b.value through Cell::set, but the uow's atomic set
    // only covers a.value (spec §10: the transaction members realize exactly
    // the declared atomic set).
    const char* source = R"(
        struct Cell {
            i32 value;
        }

        impl Cell {
            i32 set(i32 v) writes(self.value) {
                this.value = v;
                return this.value;
            }
        }

        uow Move {
            atomic: a.value
        }

        void moveValue(Cell a, Cell b)
            uow (Move)
        {
            b.set(1);
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
        }

        void tick()
            uow (Replicate)
        {
            printf("tick\n");
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_RETRY_UNSAFE_EFFECT));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationUow, ExternalIoInInlineAfterCommitIsSafe)
{
    // external_io inside the member's inline after_commit block sits outside
    // the retry window — the same placement rules as a phase member, written
    // inline (phase suffixes on the attachment are gone).
    const char* source = R"(
        import std::sys;

        uow Notify {
            strategy: database
            effects: external_io
        }

        void done()
            uow (Notify)
        {
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

TEST(VerificationUow, WholeBodyPhaseAttachment)
{
    // `uow (Name.after_commit)` runs the entire body in that phase — the
    // compact form of writing everything inside an inline after_commit block
    // (external_io is fine there).
    const char* source = R"(
        import std::sys;

        uow Notify {
            strategy: database
            effects: external_io
        }

        void done()
            uow (Notify.after_commit)
        {
            printf("done\n");
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
        struct Ledger {
            i32 total;
        }

        uow Dual {
            access: shared ledger, exclusive ledger
        }

        void touch(Ledger ledger)
            uow (Dual)
        {
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
        struct Account {
            i32 id;
        }

        uow A {
            ordering: Account.id ascending
        }

        uow B {
            ordering: Account.id descending
        }

        void first(Account account)
            uow (A)
        {
        }

        void second(Account account)
            uow (B)
        {
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
        struct Box {
            i32 value;
        }

        impl Box {
            i32 set(i32 v) writes(self.value) {
                this.value = v;
                return this.value;
            }
        }

        uow Ghost {
            writes: box.value
            atomic: box.value
            strategy: database
        }

        void doWork(Box box)
            uow (Ghost)
        {
            box.set(3);
        }

        i32 main() {
            return 7;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options(true));
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, DJINN_EXIT(7));
    ASSERT_EQ(result.verification.unitsOfWork.size(), 1);
    EXPECT_EQ(result.verification.unitsOfWork[0].members.size(), 1);
}

TEST(VerificationUow, RemovedBodyBlockIsRejectedWithMigrationHint)
{
    // Lifecycle blocks moved onto functions: `uow (Name)` next to the
    // member's body is the only way to implement a phase now.
    const char* source = R"(
        uow Old {
            body {
            }
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationUow, UnknownUowAttachmentIsHardError)
{
    const char* source = R"(
        void stray()
            uow (Nowhere)
        {
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_UNKNOWN_UOW));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationUow, UowWithoutMembersWarnsButCompiles)
{
    const char* source = R"(
        uow Lonely {
            reads: box.value
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_UOW_NO_MEMBERS));
    EXPECT_GE(warningCount(result), 1);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(VerificationUow, InheritedRequireInProveModeIsProvedOrRejected)
{
    // The uow's require is inherited by every member; in mode prove it must
    // be decidable at compile time — a parameter comparison is not.
    const char* source = R"(
        uow Positive {
            require(amount > 0) in mode prove
        }

        void work(i32 amount)
            uow (Positive)
        {
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_UNPROVABLE));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationUow, InheritedClauseOutsideMemberScopeIsAssumed)
{
    // `from` is not a parameter of `work`, so the clause cannot be inherited:
    // it stays on the audit trail as an assumed obligation.
    const char* source = R"(
        struct Box {
            i32 value;
        }

        uow Mixed {
            require(from.value > 0)
        }

        void work(i32 amount)
            uow (Mixed)
        {
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_EQ(errorCount(result), 0);
    EXPECT_EQ(result.returnCode, 0);
    const auto* require = find_obligation(result, "work", "require");
    ASSERT_NE(require, nullptr);
    EXPECT_EQ(require->status, djinn::verification::ObligationStatus::Assumed);
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

TEST(VerificationModes, IgnoreEmitsNoRuntimeCheck)
{
    // Only `in mode check` reaches the binary (spec §5.2): the runtime
    // violation below does not throw, and the program returns normally.
    const char* source = R"(
        i32 flaky(i32 x)
            ensure(return < 100) in mode ignore
        {
            return x + 200;
        }

        i32 main() throws {
            return flaky(0);
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options(true));
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, DJINN_EXIT(200));
}

//
// Transactional uow surface (VERIFICATION-SPEC.md §10): the function-level
// catch suffix, bare `rollback;` with whole-object entry snapshots, inline
// before_commit/after_commit blocks, the lock statement as atomic-discharge
// evidence, and the ContractViolation coverage rule.
//

TEST(VerificationTransactional, AtomicWithoutLockIsHardError)
{
    // Under no discharging strategy, every atomic path must be written inside
    // a lock scope naming its root object (E-CONTRACT-053).
    const char* source = R"(
        struct Cell {
            i32 value;
        }

        impl Cell {
            i32 set(i32 v) writes(self.value) {
                this.value = v;
                return this.value;
            }
        }

        uow Swap {
            atomic: a.value
        }

        void moveValue(Cell a, Cell b)
            uow (Swap)
        {
            a.set(1);
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_ATOMIC_NOT_DISCHARGED));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationTransactional, AtomicWithLockPasses)
{
    const char* source = R"(
        struct Cell {
            i32 value;
        }

        impl Cell {
            i32 set(i32 v) writes(self.value) {
                this.value = v;
                return this.value;
            }
        }

        uow Swap {
            atomic: a.value
        }

        void moveValue(Cell a, Cell b)
            uow (Swap)
        {
            lock (a) {
                a.set(1);
            }
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(VerificationTransactional, AwaitInsideLockIsRejected)
{
    const char* source = R"(
        struct Cell {
            i32 value;
        }

        async i32 fetch() {
            return 1;
        }

        void work(Cell cell) {
            lock (cell) {
                await fetch();
            }
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::AWAIT_INSIDE_LOCK));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationTransactional, RollbackCoverageIsRequired)
{
    // The member can fail mid-transaction (bump's check-mode require) and
    // writes an atomic path; without any rollback statement the all-or-nothing
    // property is unverified (E-CONTRACT-054).
    const char* source = R"(
        struct Box {
            i32 value;
        }

        impl Box {
            i32 bump(i32 v)
                require(v > 0)
                writes(self.value)
            {
                this.value = v;
                return this.value;
            }
        }

        uow Move {
            atomic: b.value
        }

        void move(Box b, i32 v)
            uow (Move)
        {
            b.bump(v);
        } catch (ContractViolation violation) {
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_ROLLBACK_INCOMPLETE));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationTransactional, BareRollbackCoversEverything)
{
    // A bare `rollback;` restores every struct parameter wholesale, so the
    // atomic paths are covered without listing them.
    const char* source = R"(
        struct Box {
            i32 value;
        }

        impl Box {
            i32 bump(i32 v)
                require(v > 0)
                writes(self.value)
            {
                this.value = v;
                return this.value;
            }
        }

        uow Move {
            atomic: b.value
        }

        void move(Box b, i32 v)
            uow (Move)
        {
            b.bump(v);
        } catch (ContractViolation violation) {
            rollback;
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(VerificationTransactional, ContractViolationMustBeCaught)
{
    // A uow member whose callee carries check-mode contracts must handle
    // ContractViolation (or the clauses must be in mode prove).
    const char* source = R"(
        struct Box {
            i32 value;
        }

        struct DomainError : Exception;

        impl Box {
            i32 bump(i32 v)
                require(v > 0)
                writes(self.value)
            {
                this.value = v;
                return this.value;
            }
        }

        uow Move {
            atomic: b.value
        }

        void move(Box b, i32 v)
            uow (Move)
        {
            b.bump(v);
        } catch (DomainError error) {
            rollback;
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_VIOLATION_UNHANDLED));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationTransactional, CatchSuffixFreesCallerFromTry)
{
    // A member that handles its own errors is non-throwing for callers:
    // main needs neither `throws` nor a try around the call.
    const char* source = R"(
        struct Box {
            i32 value;
        }

        impl Box {
            i32 bump(i32 v)
                require(v > 0)
                writes(self.value)
            {
                this.value = v;
                return this.value;
            }
        }

        void work(Box b, i32 v) {
            b.bump(v);
        } catch (ContractViolation violation) {
        }

        i32 main() {
            Box b = { 0 };
            work(b, 0 - 5);
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(VerificationTransactional, InlineAfterCommitRunsOnlyOnSuccess)
{
    // The after_commit block runs after a successful body; a contract
    // violation in the body aborts the uow and skips it (the catch handler
    // runs instead, and main still returns normally).
    const char* source = R"(
        import std::sys;

        i32 boom(i32 v)
            require(v > 0)
        {
            return v;
        }

        uow Probe {
        }

        void go(i32 v)
            uow (Probe)
        {
            boom(v);
            after_commit {
                boom(0);
            }
        } catch (ContractViolation violation) {
        }

        i32 main() {
            go(0 - 1);
            return 42;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options(true));
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, DJINN_EXIT(42));
}

TEST(VerificationTransactional, CommitSplitsTheTransactionWindow)
{
    // `commit;` ends the transaction: a non-idempotent effect before it sits
    // in the retry window (9503) — the fix is moving it after the commit.
    const char* source = R"(
        import std::sys;

        uow Probe {
            retry: allowed
        }

        void work()
            uow (Probe)
        {
            printf("before\n");
            commit;
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_TRUE(hasErrorCode(result, DiagnosticCode::E_CONTRACT_RETRY_UNSAFE_EFFECT));
    EXPECT_EQ(result.returnCode, 1);
}

TEST(VerificationTransactional, CommitAllowsExternalIoAfterIt)
{
    const char* source = R"(
        import std::sys;

        uow Probe {
            retry: allowed
        }

        void work()
            uow (Probe)
        {
            commit;
            printf("after\n");
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(VerificationTransactional, UowSectionsRequireColonSyntax)
{
    // The parenthesized section form is only valid on function signatures;
    // inside a uow, sections are always `key: value`.
    const char* source = R"(
        struct Cell {
            i32 value;
        }

        uow Swap {
            atomic(a.value)
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, report_options());
    EXPECT_EQ(result.returnCode, 1);
    EXPECT_EQ(errorCount(result), 1);
}
