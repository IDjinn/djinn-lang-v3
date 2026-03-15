//
// ScopeGuard implementation
//

#include "ScopeGuard.h"
#include "../Binder.h"
#include "../../utils/Logger.h"

namespace djinn::binder {
    ScopeGuard::ScopeGuard(Binder &binder) : _binder(binder) {
        _binder.pushScope();
    }

    ScopeGuard::ScopeGuard(Binder &binder, const ScopeType scopeType) : _binder(binder), _scopeType(scopeType) {
        _binder.pushScope();
    }

    ScopeGuard::~ScopeGuard() {
        _binder.popScope();
    }
} // namespace djinn::binder