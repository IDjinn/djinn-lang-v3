//
// ConstructorCallHandler implementation
//

#include "CallHandler.h"
#include "../Binder.h"

namespace djinn::binder {
    bool ConstructorCallHandler::canHandle(const FunctionCall &call,
                                           std::shared_ptr<ScopedSymbolTable> scope) const {
        // Don't handle method calls or enum constructions (those have ::)
        if (call.isMethodCall() || call.name.token_name.find("::") != std::string::npos) {
            return false;
        }

        // Check if the call name matches a struct that has a constructor
        if (const auto structSym = scope->lookupStruct(call.name.token_name)) {
            // Error types have no declared constructor — `MyError("message")`
            // is handled here as an implicit (str) constructor.
            if (structSym->isErrorType) {
                return true;
            }
            for (const auto &method : structSym->methods) {
                if (method->isConstructor && method->name == call.name.token_name) {
                    return true;
                }
            }
        }

        return false;
    }

    std::shared_ptr<Symbol> ConstructorCallHandler::handle(
        const FunctionCall &call,
        Binder &binder,
        std::shared_ptr<ScopedSymbolTable> scope,
        DiagnosticEngine &diagnostics) {
        const auto structSym = scope->lookupStruct(call.name.token_name);

        // Find the constructor method
        std::shared_ptr<MethodSymbol> ctor;
        for (const auto &method : structSym->methods) {
            if (method->isConstructor && method->name == call.name.token_name) {
                ctor = method;
                break;
            }
        }

        // Error construction: MyError("message") — implicit (str) constructor when
        // no explicit constructor is declared. More than 1 argument means an
        // interpolated message: the parser desugars "x {expr} y" into
        // ("x {0} y", expr), so arg 0 is the format string and the rest are values.
        if (!ctor && structSym->isErrorType) {
            std::vector<std::shared_ptr<Symbol>> parameters;
            for (const auto &arg : call.arguments) {
                parameters.emplace_back(binder.bindExpression(*arg));
            }

            return std::make_shared<FunctionCallSymbol>(
                call.name.token_name, nullptr, std::move(parameters), call.name.location
            );
        }

        // Validate argument count
        const size_t expectedArgs = ctor->callerArity();
        if (call.arguments.size() != expectedArgs)
        {
            diagnostics.emitAndPrint(Diagnostic(
                Severity::Error, DiagnosticCode::TYPE_MISMATCH,
                "constructor '" + call.name.token_name + "' expects " +
                std::to_string(expectedArgs) +
                " arguments but got " + std::to_string(call.arguments.size()),
                call.name.location));
        }

        // Bind arguments
        std::vector<std::shared_ptr<Symbol>> parameters;
        for (const auto &arg : call.arguments) {
            parameters.emplace_back(binder.bindExpression(*arg));
        }

        // Return a FunctionCallSymbol - the generator already knows how to handle
        // constructor calls by looking up the struct name
        return std::make_shared<FunctionCallSymbol>(
            call.name.token_name, nullptr, std::move(parameters), call.name.location
        );
    }
} // namespace djinn::binder
