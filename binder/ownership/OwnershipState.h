//
// Ownership and Borrow Checker - Core State Tracking
//

#ifndef DJINN_OWNERSHIP_STATE_H
#define DJINN_OWNERSHIP_STATE_H

#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include "../../diagnostics/Diagnostic.h"

namespace djinn::ownership {
    // Represents the current ownership state of a value
    enum class OwnershipStatus : uint8_t {
        Owned, // Value is owned, can be used or moved
        Moved, // Value has been moved, cannot be used
        PartiallyMoved, // Some fields have been moved (for structs)
        Borrowed, // Value is borrowed (immutable reference active)
        MutablyBorrowed // Value is mutably borrowed (exclusive access)
    };

    // Type of borrow
    enum class BorrowKind : uint8_t {
        Shared, // &T - immutable borrow, multiple allowed
        Mutable // &mut T - mutable borrow, exclusive
    };

    // Represents an active borrow
    struct ActiveBorrow {
        std::string borrowerName; // Name of variable holding the reference
        BorrowKind kind;
        SourceLocation location; // Where the borrow was created
        uint32_t scopeDepth; // Scope level where borrow was created

        ActiveBorrow(std::string name, BorrowKind k, SourceLocation loc, uint32_t depth)
            : borrowerName(std::move(name)), kind(k), location(loc), scopeDepth(depth) {
        }
    };

    // Tracks the ownership state of a single variable
    struct VariableOwnership {
        std::string name;
        OwnershipStatus status = OwnershipStatus::Owned;
        std::vector<ActiveBorrow> activeBorrows;
        SourceLocation definitionLocation;
        SourceLocation lastMoveLocation; // Where it was moved (if moved)
        uint32_t definitionScopeDepth = 0;
        bool isCopyType = false; // Types like i32, f64 are implicitly copyable

        explicit VariableOwnership(std::string n, SourceLocation loc = {}, uint32_t depth = 0)
            : name(std::move(n)), definitionLocation(loc), definitionScopeDepth(depth) {
        }

        [[nodiscard]] bool isOwned() const { return status == OwnershipStatus::Owned; }
        [[nodiscard]] bool isMoved() const { return status == OwnershipStatus::Moved; }

        [[nodiscard]] bool isBorrowed() const {
            return status == OwnershipStatus::Borrowed || status == OwnershipStatus::MutablyBorrowed;
        }

        [[nodiscard]] bool hasMutableBorrow() const {
            return std::ranges::any_of(activeBorrows,
                                       [](const ActiveBorrow &b) { return b.kind == BorrowKind::Mutable; });
        }

        [[nodiscard]] bool hasAnyBorrow() const { return !activeBorrows.empty(); }
        [[nodiscard]] size_t borrowCount() const { return activeBorrows.size(); }

        void markMoved(SourceLocation loc) {
            status = OwnershipStatus::Moved;
            lastMoveLocation = loc;
            activeBorrows.clear();
        }

        void markBorrowed(const std::string &borrower, BorrowKind kind, SourceLocation loc, uint32_t depth) {
            activeBorrows.emplace_back(borrower, kind, loc, depth);
            status = (kind == BorrowKind::Mutable)
                         ? OwnershipStatus::MutablyBorrowed
                         : OwnershipStatus::Borrowed;
        }

        void releaseBorrow(const std::string &borrower) {
            activeBorrows.erase(
                std::remove_if(activeBorrows.begin(), activeBorrows.end(),
                               [&](const ActiveBorrow &b) { return b.borrowerName == borrower; }),
                activeBorrows.end()
            );
            updateStatusAfterRelease();
        }

        void releaseBorrowsAtDepth(uint32_t depth) {
            activeBorrows.erase(
                std::remove_if(activeBorrows.begin(), activeBorrows.end(),
                               [depth](const ActiveBorrow &b) { return b.scopeDepth >= depth; }),
                activeBorrows.end()
            );
            updateStatusAfterRelease();
        }

        void reset() {
            status = OwnershipStatus::Owned;
            activeBorrows.clear();
        }

    private:
        void updateStatusAfterRelease() {
            if (activeBorrows.empty()) {
                if (status != OwnershipStatus::Moved) {
                    status = OwnershipStatus::Owned;
                }
            } else if (hasMutableBorrow()) {
                status = OwnershipStatus::MutablyBorrowed;
            } else {
                status = OwnershipStatus::Borrowed;
            }
        }
    };

    // Convert status to string for diagnostics
    inline std::string ownershipStatusToString(OwnershipStatus status) {
        switch (status) {
            case OwnershipStatus::Owned: return "owned";
            case OwnershipStatus::Moved: return "moved";
            case OwnershipStatus::PartiallyMoved: return "partially moved";
            case OwnershipStatus::Borrowed: return "borrowed";
            case OwnershipStatus::MutablyBorrowed: return "mutably borrowed";
            default: return "unknown";
        }
    }

    inline std::string borrowKindToString(BorrowKind kind) {
        switch (kind) {
            case BorrowKind::Shared: return "shared";
            case BorrowKind::Mutable: return "mutable";
            default: return "unknown";
        }
    }
} // namespace djinn::ownership

#endif // DJINN_OWNERSHIP_STATE_H