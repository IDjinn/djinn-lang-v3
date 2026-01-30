//
// Function call and identifier binding
//

#include "../Binder.h"
#include "../handlers/CallHandler.h"

std::shared_ptr<Symbol> Binder::bindIdentifier(const Identifier &id) const {
    if (const auto variableSymbol = _current_scope->lookupVariable(id.identifier.token_name); variableSymbol) {
        _current_scope->markUsed(id.identifier.token_name);
        return variableSymbol;
    }

    if (const auto functionSymbol = _current_scope->lookupFunction(id.identifier.token_name); functionSymbol) {
        return functionSymbol;
    }

    if (const auto structSymbol = _current_scope->lookupStruct(id.identifier.token_name); structSymbol) {
        return structSymbol;
    }

    BINDER_ERROR(DiagnosticCode::UNDEFINED_VARIABLE, "undefined variable '" + id.identifier.token_name + "'", id,
                 id.identifier.location);
    return nullptr;
}

std::shared_ptr<Symbol> Binder::bindFunctionCall(const FunctionCall &call) {
    // Use chain of responsibility pattern with call handlers
    static auto handlers = djinn::binder::createCallHandlers();

    for (auto &handler: handlers) {
        if (handler->canHandle(call, _current_scope)) {
            return handler->handle(call, *this, _current_scope, _diagnostics);
        }
    }

    // Should never reach here as RegularFunctionCallHandler always handles
    BINDER_ERROR(DiagnosticCode::UNDEFINED_FUNCTION, "undefined function '" + call.name.token_name + "'", call,
                 call.name.location);
    return nullptr;
}