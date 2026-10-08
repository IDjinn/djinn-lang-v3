#ifndef DJINN_SMT_SOLVER_H
#define DJINN_SMT_SOLVER_H

#include <string>
#include <utility>
#include <vector>

namespace djinn::verification
{
    enum class SmtOutcome
    {
        // Unsat: no input violates the obligation.
        Proven,
        // Sat: a counterexample is available in the model.
        Violated,
        // Outside the decidable fragment, or the solver gave up.
        Unknown,
        // No solver backend configured.
        Unavailable
    };

    struct SmtResult
    {
        SmtOutcome outcome = SmtOutcome::Unavailable;
        // Violating assignment when outcome == Violated ("amount" -> "-5").
        std::vector<std::pair<std::string, std::string>> model;
        std::string smtlib;
    };

    // Solver interface (spec §6.3): structural checks never depend on a
    // concrete backend; a Z3 integration plugs in here.
    class SmtSolver
    {
    public:
        virtual ~SmtSolver() = default;

        // Query in SMT-LIB v2 over quantifier-free linear arithmetic,
        // with free symbols already declared by the caller.
        virtual SmtResult check(const std::string& smtlib) = 0;
    };
}

#endif //DJINN_SMT_SOLVER_H
