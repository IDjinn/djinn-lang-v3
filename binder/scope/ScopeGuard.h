//
// RAII scope guard for automatic scope management
//

#ifndef DJINN_SCOPE_GUARD_H
#define DJINN_SCOPE_GUARD_H
#include <string>

class Binder; // Forward declaration in global namespace

namespace djinn::binder {
    enum class ScopeType {
        UNKNOWN,

        IF,
        ELSE,
        FOR,
        WHILE,
        DO_WHILE,
        SWITCH,
        CASE,
    };

    [[maybe_unused]] inline std::string scopeTypeToString(ScopeType scopeType) {
        switch (scopeType) {
            case ScopeType::IF:
                return "IF";
            case ScopeType::ELSE:
                return "ELSE";
            case ScopeType::FOR:
                return "FOR";
            case ScopeType::WHILE:
                return "WHILE";
            case ScopeType::DO_WHILE:
                return "DO_WHILE";
            case ScopeType::SWITCH:
                return "SWITCH";
            case ScopeType::CASE:
                return "CASE";
            default:
                return "UNKNOWN";
        }
    }

    class ScopeGuard {
    public:
        explicit ScopeGuard(Binder &binder);

        explicit ScopeGuard(Binder &binder, ScopeType scopeType);

        ~ScopeGuard();

        // Non-copyable, non-movable
        ScopeGuard(const ScopeGuard &) = delete;

        ScopeGuard &operator=(const ScopeGuard &) = delete;

        ScopeGuard(ScopeGuard &&) = delete;

        ScopeGuard &operator=(ScopeGuard &&) = delete;

    private:
        Binder &_binder;
        ScopeType _scopeType;
    };
} // namespace djinn::binder

#endif // DJINN_SCOPE_GUARD_H