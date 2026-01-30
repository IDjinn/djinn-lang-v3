//
// MethodCallHandler implementation
//

#include "CallHandler.h"
#include "../Binder.h"

namespace djinn::binder {
    bool MethodCallHandler::canHandle(const FunctionCall &call,
                                      std::shared_ptr<ScopedSymbolTable> /*scope*/) const {
        return call.isMethodCall();
    }

    std::shared_ptr<Symbol> MethodCallHandler::handle(
        const FunctionCall &call,
        Binder &binder,
        std::shared_ptr<ScopedSymbolTable> /*scope*/,
        DiagnosticEngine & /*diagnostics*/) {
        const auto receiver = binder.bindExpression(*call.receiver);
        if (!receiver) return nullptr;

        std::vector<std::shared_ptr<Symbol> > parameters;
        for (const auto &arg: call.arguments) {
            parameters.emplace_back(binder.bindExpression(*arg));
        }

        // TODO: Validate method exists on the struct type
        return std::make_shared<FunctionCallSymbol>(
            call.name.token_name, receiver, nullptr,
            std::move(parameters), call.name.location
        );
    }
} // namespace djinn::binder