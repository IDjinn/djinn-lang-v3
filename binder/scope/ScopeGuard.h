//
// RAII scope guard for automatic scope management
//

#ifndef DJINN_SCOPE_GUARD_H
#define DJINN_SCOPE_GUARD_H

class Binder; // Forward declaration in global namespace

namespace djinn::binder {
    class ScopeGuard {
    public:
        explicit ScopeGuard(Binder &binder);

        ~ScopeGuard();

        // Non-copyable, non-movable
        ScopeGuard(const ScopeGuard &) = delete;

        ScopeGuard &operator=(const ScopeGuard &) = delete;

        ScopeGuard(ScopeGuard &&) = delete;

        ScopeGuard &operator=(ScopeGuard &&) = delete;

    private:
        Binder &_binder;
    };
} // namespace djinn::binder

#endif // DJINN_SCOPE_GUARD_H
