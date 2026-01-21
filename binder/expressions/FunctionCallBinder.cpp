//
// Function call and identifier binding
//

#include "../Binder.h"
#include "../../generator/Intrinsics.h"

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
    if (call.isMethodCall()) {
        const auto receiver = bindExpression(*call.receiver);
        if (!receiver) return nullptr;

        std::vector<std::shared_ptr<Symbol> > parameters;
        for (const auto &arg: call.arguments) {
            parameters.emplace_back(bindExpression(*arg));
        }

        // TODO: Validate method exists on the struct type
        return std::make_shared<FunctionCallSymbol>(
            call.name.token_name, receiver, nullptr,
            std::move(parameters), call.name.location
        );
    }

    if (is_intrinsic(call.name.token_name)) {
        std::vector<std::shared_ptr<Symbol> > parameters;
        for (const auto &arg: call.arguments) {
            parameters.emplace_back(bindExpression(*arg));
        }

        return _current_scope->defineIntrisicCall(call.name.token_name, parameters);
    }

    // Enum::Variant(args)
    if (const auto colonPos = call.name.token_name.find("::"); colonPos != std::string::npos) {
        const std::string enumName = call.name.token_name.substr(0, colonPos);
        const std::string variantName = call.name.token_name.substr(colonPos + 2);

        if (const auto enumSym = _current_scope->lookupEnum(enumName)) {
            if (enumSym->hasVariant(variantName)) {
                if (const auto *variant = enumSym->getVariant(variantName);
                    variant && call.arguments.size() != variant->associatedTypes.size()) {
                    BINDER_ERROR(DiagnosticCode::TYPE_MISMATCH,
                                 "function '" + call.name.token_name + "' expects " + std::to_string(variant->
                                     associatedTypes.size()) +
                                 " arguments but got " + std::to_string(call.arguments.size()), call,
                                 call.name.location);
                }

                std::vector<std::shared_ptr<Symbol> > parameters;
                for (const auto &arg: call.arguments) {
                    parameters.emplace_back(bindExpression(*arg));
                }
                return std::make_shared<EnumConstructionSymbol>(
                    enumName, variantName, enumSym->getVariantTag(variantName),
                    std::move(parameters), call.name.location
                );
            }

            BINDER_ERROR(DiagnosticCode::UNDEFINED_FUNCTION, "undefined function '" + call.name.token_name + "'", call,
                         call.name.location);
            return nullptr;
        }
    }

    const auto funcSym = _current_scope->lookupFunction(call.name.token_name);
    if (!funcSym) {
        BINDER_ERROR(DiagnosticCode::UNDEFINED_FUNCTION, "undefined function '" + call.name.token_name + "'", call,
                     call.name.location);
        return nullptr;
    }

    if (!funcSym->isVariadic && call.arguments.size() != funcSym->arity()) {
        BINDER_ERROR(DiagnosticCode::TYPE_MISMATCH,
                     "function '" + call.name.token_name + "' expects " + std::to_string(funcSym->arity()) +
                     " arguments but got " + std::to_string(call.arguments.size()), call, call.name.location);
    } else if (funcSym->isVariadic && call.arguments.size() < funcSym->arity()) {
        BINDER_ERROR(DiagnosticCode::TYPE_MISMATCH,
                     "function '" + call.name.token_name + "' expects " + std::to_string(funcSym->arity()) +
                     " arguments but got " + std::to_string(call.arguments.size()), call, call.name.location);
    }

    std::vector<std::shared_ptr<Symbol> > parameters;
    for (const auto &arg: call.arguments) {
        parameters.emplace_back(bindExpression(*arg));
    }
    return std::make_shared<FunctionCallSymbol>(
        call.name.token_name, funcSym, std::move(parameters), call.name.location
    );
}
