//
// EnumConstructionHandler implementation
//

#include "CallHandler.h"
#include "../Binder.h"

namespace djinn::binder {
    bool EnumConstructionHandler::canHandle(const FunctionCall &call,
                                            std::shared_ptr<ScopedSymbolTable> scope) const {
        const auto colonPos = call.name.token_name.find("::");
        if (colonPos == std::string::npos) {
            return false;
        }

        const std::string enumName = call.name.token_name.substr(0, colonPos);
        const std::string variantName = call.name.token_name.substr(colonPos + 2);

        if (const auto enumSym = scope->lookupEnum(enumName)) {
            return enumSym->hasVariant(variantName);
        }

        return false;
    }

    std::shared_ptr<Symbol> EnumConstructionHandler::handle(
        const FunctionCall &call,
        Binder &binder,
        std::shared_ptr<ScopedSymbolTable> scope,
        DiagnosticEngine &diagnostics) {
        const auto colonPos = call.name.token_name.find("::");
        const std::string enumName = call.name.token_name.substr(0, colonPos);
        const std::string variantName = call.name.token_name.substr(colonPos + 2);

        const auto enumSym = scope->lookupEnum(enumName);

        if (const auto *variant = enumSym->getVariant(variantName);
            variant && call.arguments.size() != variant->associatedTypes.size()) {
            diagnostics.emitAndPrint(Diagnostic(
                Severity::Error, DiagnosticCode::TYPE_MISMATCH,
                "function '" + call.name.token_name + "' expects " +
                std::to_string(variant->associatedTypes.size()) +
                " arguments but got " + std::to_string(call.arguments.size()),
                call.name.location));
        }

        std::vector<std::shared_ptr<Symbol> > parameters;
        for (const auto &arg: call.arguments) {
            parameters.emplace_back(binder.bindExpression(*arg));
        }

        return std::make_shared<EnumConstructionSymbol>(
            enumName, variantName, enumSym->getVariantTag(variantName),
            std::move(parameters), call.name.location
        );
    }
} // namespace djinn::binder