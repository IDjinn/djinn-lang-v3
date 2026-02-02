//
// Ownership Tracker - Manages ownership state across scopes
//

#ifndef DJINN_OWNERSHIP_TRACKER_H
#define DJINN_OWNERSHIP_TRACKER_H

#include <map>
#include <stack>
#include <vector>
#include <memory>
#include <functional>
#include "OwnershipState.h"
#include "../Symbol.h"
#include "../../parser/ast/Type.h"

namespace djinn::ownership {
    // Result of an ownership check
    struct OwnershipCheckResult {
        bool success = true;
        std::string errorMessage;
        SourceLocation errorLocation;
        std::optional<SourceLocation> relatedLocation; // e.g., where value was moved

        static OwnershipCheckResult ok() { return {}; }

        static OwnershipCheckResult error(std::string msg, SourceLocation loc,
                                          std::optional<SourceLocation> related = std::nullopt) {
            return {false, std::move(msg), loc, related};
        }
    };

    class OwnershipTracker {
    public:
        OwnershipTracker() = default;

        // Scope management
        void pushScope();

        void popScope();

        [[nodiscard]] uint32_t currentScopeDepth() const { return _scopeDepth; }

        // Variable lifecycle
        void defineVariable(const std::string &name, const Type &type, SourceLocation loc);

        void undefineVariable(const std::string &name);

        // Ownership operations
        [[nodiscard]] OwnershipCheckResult checkUse(const std::string &name, SourceLocation loc) const;

        [[nodiscard]] OwnershipCheckResult checkMove(const std::string &name, SourceLocation loc);

        [[nodiscard]] OwnershipCheckResult checkBorrow(const std::string &name, BorrowKind kind,
                                                       const std::string &borrower, SourceLocation loc);

        [[nodiscard]] OwnershipCheckResult checkAssign(const std::string &name, SourceLocation loc);

        // Execute a move operation (after validation)
        void doMove(const std::string &name, SourceLocation loc);

        // Execute a borrow operation (after validation)
        void doBorrow(const std::string &name, BorrowKind kind,
                      const std::string &borrower, SourceLocation loc);

        // Release a borrow when borrower goes out of scope
        void releaseBorrow(const std::string &borrower);

        // Re-initialize a moved variable (e.g., through assignment)
        void reinitialize(const std::string &name);

        // Query state
        [[nodiscard]] bool isVariableTracked(const std::string &name) const;

        [[nodiscard]] std::optional<OwnershipStatus> getStatus(const std::string &name) const;

        [[nodiscard]] static bool isCopyType(const Type &type);

        // Get all variables with active borrows in current scope
        [[nodiscard]] std::vector<std::pair<std::string, VariableOwnership *> > getActiveBorrows() const;

        // Diagnostics
        [[nodiscard]] std::string getOwnershipDiagnostic(const std::string &name) const;

    private:
        std::map<std::string, VariableOwnership> _variables;
        std::stack<std::vector<std::string> > _scopeVariables; // Variables defined in each scope
        uint32_t _scopeDepth = 0;

        // Track which variables are references (borrowers)
        std::map<std::string, std::string> _borrowerToSource; // borrower -> source variable

        [[nodiscard]] VariableOwnership *getVariable(const std::string &name);

        [[nodiscard]] const VariableOwnership *getVariable(const std::string &name) const;
    };
} // namespace djinn::ownership

#endif // DJINN_OWNERSHIP_TRACKER_H