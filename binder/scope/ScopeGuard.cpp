//
// ScopeGuard implementation
//

#include "ScopeGuard.h"
#include "../Binder.h"
#include "../../utils/Logger.h"

namespace djinn::binder {
    ScopeGuard::ScopeGuard(Binder &binder) : _binder(binder) {
        _binder.pushScope();
        LOG_TRACE("entering scope");
    }

    ScopeGuard::ScopeGuard(Binder &binder, const ScopeType scopeType) : _binder(binder), _scopeType(scopeType) {
        _binder.pushScope();
        LOG_TRACE("entering scope %s", scopeTypeToString(scopeType).c_str());
    }

    ScopeGuard::~ScopeGuard() {
        LOG_TRACE("leaving scope");
        _binder.popScope();
    }
} // namespace djinn::binder