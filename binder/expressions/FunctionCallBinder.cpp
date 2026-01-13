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
    // Handle method calls (receiver.method())
    if (call.isMethodCall()) {
        // Check if receiver is an Identifier (could be struct name.token_name for static method)
        if (const auto *ident = dynamic_cast<const Identifier *>(call.receiver.get())) {
            // If it's a struct name.token_name, this is a static method call - don't bind as variable
            if (!_global_scope->lookupStruct(ident->identifier.token_name)) {
                // Not a struct name.token_name, bind as regular expression (instance method)
                bindExpression(*call.receiver);
            }
        } else {
            // Not an identifier, bind the receiver expression
            bindExpression(*call.receiver);
        }

        // Bind all arguments
        for (const auto &arg: call.arguments) {
            bindExpression(*arg);
        }

        // TODO: Validate method exists on the struct type
        return;
    }

    if (is_intrinsic(call.name.token_name)) {
        // Bind arguments for intrinsics
        for (const auto &arg: call.arguments) {
            bindExpression(*arg);
        }
        return;
    }

    // Check for enum construction: Enum::Variant(args)
    const size_t colonPos = call.name.token_name.find("::");
    if (colonPos != std::string::npos) {
        const std::string enumName = call.name.token_name.substr(0, colonPos);
        const std::string variantName = call.name.token_name.substr(colonPos + 2);

        if (const auto enumSym = _global_scope->lookupEnum(enumName)) {
            if (enumSym->hasVariant(variantName)) {
                // Valid enum construction - bind arguments
                const auto *variant = enumSym->getVariant(variantName);
                if (variant && call.arguments.size() != variant->associatedTypes.size()) {
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
            // Enum exists but variant doesn't
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
