#include <gtest/gtest.h>
#include "TestHelpers.h"

#include "DjinnCompiler.h"
#include "config/ProjectConfig.h"
#include "verification/IR.h"
#include "verification/Verifier.h"
#include "verification/smt/SmtSolver.h"

//
// Verification pass scaffolding (`--verify off|report|trace`, djinn.proj
// `compiler.verify`). Report/Trace build the Verification IR into
// CompilerResult::verification and log in Trace mode. Violations the pass
// PROVES over the decidable fragment are hard errors (see phase2.cpp);
// programs whose contracts hold compile identically with the pass on or off.
//

namespace
{
    const char* CONTRACT_SOURCE = R"(
        i32 my_abs(i32 value)
            require(value > -100000)
            ensure(return >= 0)
        {
            return value < 0 ? -value : value;
        }

        i32 main() {
            i32 result = try my_abs(-5) ?: 0;
            return result;
        }
    )";
}

TEST(Verification, VerifyOffCompilesUnchanged)
{
    const auto result = DjinnCompiler::run(CONTRACT_SOURCE, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
    EXPECT_EQ(result.verification.obligations.size(), 0);
}

TEST(Verification, VerifyReportKeepsBehaviorIdentical)
{
    CompilerOptions options;
    options.generateBinary = true;
    options.verificationMode = VerificationMode::Report;

    const auto off = DjinnCompiler::run(CONTRACT_SOURCE, {.generateBinary = true});
    const auto report = DjinnCompiler::run(CONTRACT_SOURCE, options);

    EXPECT_EQ(report.diagnostics.size(), 0);
    EXPECT_EQ(report.returnCode, off.returnCode);
}

TEST(Verification, VerifyReportRunsBeforeCodegen)
{
    // The pass runs between binding and code generation (both entry points);
    // a clean program must pass through it untouched.
    CompilerOptions options;
    options.generateBinary = true;
    options.verificationMode = VerificationMode::Report;

    const auto result = DjinnCompiler::run(CONTRACT_SOURCE, options);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(Verification, VerifyReportCollectsObligationsIntoResult)
{
    CompilerOptions options;
    options.generateBinary = true;
    options.verificationMode = VerificationMode::Report;

    const auto result = DjinnCompiler::run(CONTRACT_SOURCE, options);
    EXPECT_EQ(result.diagnostics.size(), 0);

    // my_abs contributes one require and one ensure; std symbols may add more.
    const auto& obligations = result.verification.obligations;
    EXPECT_GE(obligations.size(), 2);

    bool has_require = false;
    bool has_ensure = false;
    for (const auto& obligation : obligations)
    {
        EXPECT_FALSE(obligation.subject.empty());
        EXPECT_NE(obligation.status, djinn::verification::ObligationStatus::Unverified);
        if (obligation.subject == "my_abs" && obligation.description == "require")
        {
            has_require = true;
            // Requires are preconditions: assumed in the body, runtime-checked.
            EXPECT_EQ(obligation.status, djinn::verification::ObligationStatus::Checked);
        }
        if (obligation.subject == "my_abs" && obligation.description == "ensure")
        {
            has_ensure = true;
            // The ternary body splits into two return paths and both
            // establish return >= 0 over the decidable fragment.
            EXPECT_EQ(obligation.status, djinn::verification::ObligationStatus::Proven);
            EXPECT_FALSE(obligation.justification.empty());
        }
    }
    EXPECT_TRUE(has_require);
    EXPECT_TRUE(has_ensure);
}

TEST(Verification, VerifyTraceCollectsObligationsAndKeepsBehavior)
{
    CompilerOptions options;
    options.generateBinary = true;
    options.verificationMode = VerificationMode::Trace;

    const auto result = DjinnCompiler::run(CONTRACT_SOURCE, options);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
    EXPECT_GE(result.verification.obligations.size(), 2);
}

TEST(Verification, ProjectConfigAppliesVerifyMode)
{
    ProjectConfig config;
    config.compiler.verificationMode = "trace";

    CompilerOptions options;
    config.applyTo(options);

    EXPECT_EQ(options.verificationMode, VerificationMode::Trace);
}

TEST(Verification, ProjectConfigRejectsUnknownVerifyMode)
{
    ProjectConfig config;
    config.compiler.verificationMode = "loud";

    CompilerOptions options;
    options.verificationMode = VerificationMode::Report;
    config.applyTo(options);

    EXPECT_EQ(options.verificationMode, VerificationMode::Report);
}

TEST(Verification, VerificationIrTypesAreConstructible)
{
    using namespace djinn::verification;

    Obligation obligation;
    obligation.kind = ObligationKind::Ensure;
    obligation.mode = ObligationMode::Prove;
    obligation.status = ObligationStatus::Checked;
    obligation.subject = "payments::uow/Transfer";
    obligation.description = "ensure";
    EXPECT_EQ(obligation.status, ObligationStatus::Checked);

    ClaimSet claims;
    claims.subject = "payments::uow/Transfer";
    claims.writes = {"from.balance", "to.balance"};
    claims.effects = {"database"};
    EXPECT_EQ(claims.writes.size(), 2);

    OrderingGraph graph;
    graph.add_edge("payments::uow/Transfer", "Account.id", true);
    EXPECT_EQ(graph.edges.size(), 1);

    UnitOfWork uow;
    uow.name = "Transfer";
    uow.retry = RetryPolicy::Allowed;
    uow.strategy = "database";
    EXPECT_EQ(uow.retry, RetryPolicy::Allowed);

    VerificationIR ir;
    ir.obligations.push_back(obligation);
    ir.claims.push_back(claims);
    ir.unitsOfWork.push_back(std::move(uow));
    EXPECT_EQ(ir.obligations.size(), 1);
    EXPECT_EQ(ir.unitsOfWork.size(), 1);
}

namespace
{
    struct NullSolver final : djinn::verification::SmtSolver
    {
        djinn::verification::SmtResult check(const std::string& smtlib) override
        {
            djinn::verification::SmtResult result;
            result.smtlib = smtlib;
            return result;
        }
    };
}

TEST(Verification, SmtSolverInterfaceIsPluggable)
{
    NullSolver solver;
    const auto result = solver.check("(assert (> amount 0))");

    EXPECT_EQ(result.outcome, djinn::verification::SmtOutcome::Unavailable);
    EXPECT_TRUE(result.model.empty());
    EXPECT_EQ(result.smtlib, "(assert (> amount 0))");
}
