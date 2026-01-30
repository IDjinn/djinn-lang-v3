//
// ScopeGuard implementation
//

#include "ScopeGuard.h"
#include "../Binder.h"

namespace djinn::binder {
    ScopeGuard::ScopeGuard(Binder &binder) : _binder(binder) {
        _binder.pushScope();
    }

    ScopeGuard::~ScopeGuard() {
        _binder.popScope();
    }
} // namespace djinn::binder