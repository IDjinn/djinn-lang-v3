//
// IntrinsicCallHandler implementation
//

#include "CallHandler.h"
#include "../Binder.h"
#include "../../generator/Intrinsics.h"

namespace djinn::binder {
    bool IntrinsicCallHandler::canHandle(const FunctionCall &call,
                                         std::shared_ptr<ScopedSymbolTable> /*scope*/) const {
        return is_intrinsic(call.name.token_name);
    }

    std::shared_ptr<Symbol> IntrinsicCallHandler::handle(
        const FunctionCall &call,
        Binder &binder,
        std::shared_ptr<ScopedSymbolTable> scope,
        DiagnosticEngine & /*diagnostics*/) {
        std::vector<std::shared_ptr<Symbol> > parameters;
        for (const auto &arg: call.arguments) {
            parameters.emplace_back(binder.bindExpression(*arg));
        }

        return scope->defineIntrisicCall(call.name.token_name, parameters);
    }
} // namespace djinn::binder