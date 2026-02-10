//
// RegularFunctionCallHandler implementation
//

#include "CallHandler.h"
#include "../Binder.h"

namespace djinn::binder {
    bool RegularFunctionCallHandler::canHandle(const FunctionCall & /*call*/,
                                               std::shared_ptr<ScopedSymbolTable> /*scope*/) const {
        // This is the fallback handler, so it always returns true
        // It will be the last in the chain
        return true;
    }

    std::shared_ptr<Symbol> RegularFunctionCallHandler::handle(
        const FunctionCall &call,
        Binder &binder,
        std::shared_ptr<ScopedSymbolTable> scope,
        DiagnosticEngine &diagnostics) {
        const auto funcSym = scope->lookupFunction(call.name.token_name);
        if (!funcSym) {
            diagnostics.emitAndPrint(Diagnostic(
                Severity::Error, DiagnosticCode::UNDEFINED_FUNCTION,
                "undefined function '" + call.name.token_name + "'",
                call.name.location));
            return nullptr;
        }

        if (!funcSym->isVariadic && call.arguments.size() != funcSym->arity()) {
            diagnostics.emitAndPrint(Diagnostic(
                Severity::Error, DiagnosticCode::TYPE_MISMATCH,
                "function '" + call.name.token_name + "' expects " +
                std::to_string(funcSym->arity()) +
                " arguments but got " + std::to_string(call.arguments.size()),
                call.name.location));
        } else if (funcSym->isVariadic && call.arguments.size() < funcSym->arity()) {
            diagnostics.emitAndPrint(Diagnostic(
                Severity::Error, DiagnosticCode::TYPE_MISMATCH,
                "function '" + call.name.token_name + "' expects at least " +
                std::to_string(funcSym->arity()) +
                " arguments but got " + std::to_string(call.arguments.size()),
                call.name.location));
        }

        std::vector<std::shared_ptr<Symbol> > parameters;
        for (const auto &arg: call.arguments) {
            parameters.emplace_back(binder.bindExpression(*arg));
        }

        return std::make_shared<FunctionCallSymbol>(
            call.name.token_name, funcSym, std::move(parameters), call.name.location
        );
    }

    // Factory function
    std::vector<std::unique_ptr<CallHandler> > createCallHandlers() {
        std::vector<std::unique_ptr<CallHandler> > handlers;
        handlers.push_back(std::make_unique<MethodCallHandler>());
        handlers.push_back(std::make_unique<IntrinsicCallHandler>());
        handlers.push_back(std::make_unique<EnumConstructionHandler>());
        handlers.push_back(std::make_unique<ConstructorCallHandler>());
        handlers.push_back(std::make_unique<RegularFunctionCallHandler>());
        return handlers;
    }
} // namespace djinn::binder