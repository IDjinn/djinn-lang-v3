//
// Ownership Tracker Implementation
//

#include "OwnershipTracker.h"

namespace djinn::ownership {
    void OwnershipTracker::pushScope() {
        _scopeDepth++;
        _scopeVariables.push({});
    }

    void OwnershipTracker::popScope() {
        if (_scopeDepth == 0) return;

        // Release all borrows created in this scope
        for (auto &[name, var]: _variables) {
            var.releaseBorrowsAtDepth(_scopeDepth);
        }

        // Clean up borrower tracking for variables going out of scope
        if (!_scopeVariables.empty()) {
            for (const auto &varName: _scopeVariables.top()) {
                // If this variable was a borrower, release its borrow from the source
                if (auto it = _borrowerToSource.find(varName); it != _borrowerToSource.end()) {
                    if (auto *sourceVar = getVariable(it->second)) {
                        sourceVar->releaseBorrow(varName);
                    }
                    _borrowerToSource.erase(it);
                }
            }
            _scopeVariables.pop();
        }

        _scopeDepth--;
    }

    void OwnershipTracker::defineVariable(const std::string &name, const Type &type, SourceLocation loc) {
        VariableOwnership ownership(name, loc, _scopeDepth);
        ownership.isCopyType = isCopyType(type);

        _variables.insert_or_assign(name, std::move(ownership));

        if (!_scopeVariables.empty()) {
            _scopeVariables.top().push_back(name);
        }
    }

    void OwnershipTracker::undefineVariable(const std::string &name) {
        _variables.erase(name);
    }

    OwnershipCheckResult OwnershipTracker::checkUse(const std::string &name, SourceLocation loc) const {
        const auto *var = getVariable(name);
        if (!var) {
            return OwnershipCheckResult::ok(); // Not tracked, assume OK
        }

        if (var->isMoved()) {
            return OwnershipCheckResult::error(
                "use of moved value '" + name + "'",
                loc,
                var->lastMoveLocation
            );
        }

        return OwnershipCheckResult::ok();
    }

    OwnershipCheckResult OwnershipTracker::checkMove(const std::string &name, SourceLocation loc) {
        auto *var = getVariable(name);
        if (!var) {
            return OwnershipCheckResult::ok();
        }

        // Copy types don't move, they copy
        if (var->isCopyType) {
            return OwnershipCheckResult::ok();
        }

        if (var->isMoved()) {
            return OwnershipCheckResult::error(
                "cannot move '" + name + "': value has already been moved",
                loc,
                var->lastMoveLocation
            );
        }

        if (var->hasAnyBorrow()) {
            const auto &borrow = var->activeBorrows.front();
            return OwnershipCheckResult::error(
                "cannot move '" + name + "': value is currently borrowed by '" + borrow.borrowerName + "'",
                loc,
                borrow.location
            );
        }

        return OwnershipCheckResult::ok();
    }

    OwnershipCheckResult OwnershipTracker::checkBorrow(const std::string &name, BorrowKind kind,
                                                       const std::string &borrower, SourceLocation loc) {
        auto *var = getVariable(name);
        if (!var) {
            return OwnershipCheckResult::ok();
        }

        if (var->isMoved()) {
            return OwnershipCheckResult::error(
                "cannot borrow '" + name + "': value has been moved",
                loc,
                var->lastMoveLocation
            );
        }

        if (kind == BorrowKind::Mutable) {
            // Mutable borrow requires no other borrows
            if (var->hasAnyBorrow()) {
                const auto &existingBorrow = var->activeBorrows.front();
                std::string borrowType = existingBorrow.kind == BorrowKind::Mutable
                                             ? "mutably"
                                             : "immutably";
                return OwnershipCheckResult::error(
                    "cannot borrow '" + name + "' as mutable: already " + borrowType +
                    " borrowed by '" + existingBorrow.borrowerName + "'",
                    loc,
                    existingBorrow.location
                );
            }
        } else {
            // Shared borrow requires no mutable borrows
            if (var->hasMutableBorrow()) {
                const auto &mutBorrow = *std::ranges::find_if(var->activeBorrows,
                                                              [](const ActiveBorrow &b) {
                                                                  return b.kind == BorrowKind::Mutable;
                                                              });
                return OwnershipCheckResult::error(
                    "cannot borrow '" + name + "' as immutable: already mutably borrowed by '" +
                    mutBorrow.borrowerName + "'",
                    loc,
                    mutBorrow.location
                );
            }
        }

        return OwnershipCheckResult::ok();
    }

    OwnershipCheckResult OwnershipTracker::checkAssign(const std::string &name, SourceLocation loc) {
        auto *var = getVariable(name);
        if (!var) {
            return OwnershipCheckResult::ok();
        }

        // Cannot assign to a variable while it's borrowed
        if (var->hasAnyBorrow()) {
            const auto &borrow = var->activeBorrows.front();
            return OwnershipCheckResult::error(
                "cannot assign to '" + name + "': value is currently borrowed by '" + borrow.borrowerName + "'",
                loc,
                borrow.location
            );
        }

        return OwnershipCheckResult::ok();
    }

    void OwnershipTracker::doMove(const std::string &name, SourceLocation loc) {
        if (auto *var = getVariable(name)) {
            if (!var->isCopyType) {
                var->markMoved(loc);
            }
        }
    }

    void OwnershipTracker::doBorrow(const std::string &name, BorrowKind kind,
                                    const std::string &borrower, SourceLocation loc) {
        if (auto *var = getVariable(name)) {
            var->markBorrowed(borrower, kind, loc, _scopeDepth);
            _borrowerToSource[borrower] = name;
        }
    }

    void OwnershipTracker::releaseBorrow(const std::string &borrower) {
        if (auto it = _borrowerToSource.find(borrower); it != _borrowerToSource.end()) {
            if (auto *sourceVar = getVariable(it->second)) {
                sourceVar->releaseBorrow(borrower);
            }
            _borrowerToSource.erase(it);
        }
    }

    void OwnershipTracker::reinitialize(const std::string &name) {
        if (auto *var = getVariable(name)) {
            var->reset();
        }
    }

    bool OwnershipTracker::isVariableTracked(const std::string &name) const {
        return _variables.contains(name);
    }

    std::optional<OwnershipStatus> OwnershipTracker::getStatus(const std::string &name) const {
        if (const auto *var = getVariable(name)) {
            return var->status;
        }
        return std::nullopt;
    }

    bool OwnershipTracker::isCopyType(const Type &type) {
        switch (type.kind) {
            case TypeKind::F16:
            case TypeKind::F32:
            case TypeKind::F64:
            case TypeKind::F128:
            case TypeKind::INTEGER:
            case TypeKind::VOID:
            case TypeKind::POINTER:
                return true;

            default:
                return false;
        }
    }

    std::vector<std::pair<std::string, VariableOwnership *> > OwnershipTracker::getActiveBorrows() const {
        std::vector<std::pair<std::string, VariableOwnership *> > result;
        for (auto &[name, var]: const_cast<std::map<std::string, VariableOwnership> &>(_variables)) {
            if (var.hasAnyBorrow()) {
                result.emplace_back(name, &var);
            }
        }
        return result;
    }

    std::string OwnershipTracker::getOwnershipDiagnostic(const std::string &name) const {
        const auto *var = getVariable(name);
        if (!var) {
            return "variable '" + name + "' is not tracked";
        }

        std::string result = "variable '" + name + "' is " + ownershipStatusToString(var->status);

        if (!var->activeBorrows.empty()) {
            result += " with " + std::to_string(var->activeBorrows.size()) + " active borrow(s): ";
            for (size_t i = 0; i < var->activeBorrows.size(); ++i) {
                if (i > 0) result += ", ";
                const auto &b = var->activeBorrows[i];
                result += borrowKindToString(b.kind) + " by '" + b.borrowerName + "'";
            }
        }

        if (var->isMoved() && var->lastMoveLocation.line > 0) {
            result += " (moved at line " + std::to_string(var->lastMoveLocation.line) + ")";
        }

        return result;
    }

    VariableOwnership *OwnershipTracker::getVariable(const std::string &name) {
        auto it = _variables.find(name);
        return it != _variables.end() ? &it->second : nullptr;
    }

    const VariableOwnership *OwnershipTracker::getVariable(const std::string &name) const {
        auto it = _variables.find(name);
        return it != _variables.end() ? &it->second : nullptr;
    }
} // namespace djinn::ownership