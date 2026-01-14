//
// Function call and identifier binding
//

#include "../Binder.h"
#include "../../generator/Intrinsics.h"

void Binder::bindIdentifier(const Identifier &id) const {
    if (const auto sym = _current_scope->lookupVariable(id.identifier.token_name); sym) {
        _current_scope->markUsed(id.identifier.token_name);
        return;
    }

    if (_current_scope->lookupFunction(id.identifier.token_name)) {
        return;
    }

    if (_global_scope->lookupStruct(id.identifier.token_name)) {
        return;
    }

    BINDER_ERROR(DiagnosticCode::UNDEFINED_VARIABLE, "undefined variable '" + id.identifier.token_name + "'", id,
                 id.identifier.location);
}

void Binder::bindFunctionCall(const FunctionCall &call) {
    if (call.isMethodCall()) {
        if (const auto *ident = dynamic_cast<const Identifier *>(call.receiver.get())) {
            if (!_global_scope->lookupStruct(ident->identifier.token_name)) {
                // Not a struct name.token_name, bind as regular expression (instance method)
                bindExpression(*call.receiver);
            }
        } else {
            bindExpression(*call.receiver);
        }

        for (const auto &arg: call.arguments) {
            bindExpression(*arg);
        }

        // TODO: Validate method exists on the struct type
        return;
    }

    if (is_intrinsic(call.name.token_name)) {
        for (const auto &arg: call.arguments) {
            bindExpression(*arg);
        }
        return;
    }

    // Enum::Variant(args)
    if (const auto colonPos = call.name.token_name.find("::"); colonPos != std::string::npos) {
        const std::string enumName = call.name.token_name.substr(0, colonPos);
        const std::string variantName = call.name.token_name.substr(colonPos + 2);

        if (const auto enumSym = _global_scope->lookupEnum(enumName)) {
            if (enumSym->hasVariant(variantName)) {
                if (const auto *variant = enumSym->getVariant(variantName); variant && call.arguments.size() != variant->associatedTypes.size()) {
                    BINDER_ERROR(DiagnosticCode::TYPE_MISMATCH,
                                 "function '" + call.name.token_name + "' expects " + std::to_string(variant->
                                     associatedTypes.size()) +
                                 " arguments but got " + std::to_string(call.arguments.size()), call,
                                 call.name.location);
                }
                for (const auto &arg: call.arguments) {
                    bindExpression(*arg);
                }
                return;
            }

            BINDER_ERROR(DiagnosticCode::UNDEFINED_FUNCTION, "undefined function '" + call.name.token_name + "'", call, call.name.location);
            return;
        }
    }

    if (const auto funcSym = _global_scope->lookupFunction(call.name.token_name); funcSym) {
        if (!funcSym->isVariadic && call.arguments.size() != funcSym->arity()) {
            BINDER_ERROR(DiagnosticCode::TYPE_MISMATCH,
                         "function '" + call.name.token_name + "' expects " + std::to_string(funcSym->arity()) +
                         " arguments but got " + std::to_string(call.arguments.size()), call, call.name.location);
        } else if (funcSym->isVariadic && call.arguments.size() < funcSym->arity()) {
            BINDER_ERROR(DiagnosticCode::TYPE_MISMATCH,
                         "function '" + call.name.token_name + "' expects " + std::to_string(funcSym->arity()) +
                         " arguments but got " + std::to_string(call.arguments.size()), call, call.name.location);
        }
    } else {
        BINDER_ERROR(DiagnosticCode::UNDEFINED_FUNCTION, "undefined function '" + call.name.token_name + "'", call, call.name.location);
    }

    for (const auto &arg: call.arguments) {
        bindExpression(*arg);
    }
}
