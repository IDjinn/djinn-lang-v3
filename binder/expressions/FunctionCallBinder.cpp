//
// Function call and identifier binding
//

#include "../Binder.h"
#include "../../generator/Intrinsics.h"

void Binder::bindIdentifier(const Identifier &id) const {
    if (const auto sym = _current_scope->lookupVariable(id.name); sym) {
        _current_scope->markUsed(id.name);
        return;
    }

    if (_current_scope->lookupFunction(id.name)) {
        return;
    }

    if (_global_scope->lookupStruct(id.name)) {
        return;
    }

    errorUndefinedVariable(id.name, {});
}

void Binder::bindFunctionCall(const FunctionCall &call) {
    // Handle method calls (receiver.method())
    if (call.isMethodCall()) {
        // Check if receiver is an Identifier (could be struct name for static method)
        if (const auto *ident = dynamic_cast<const Identifier *>(call.receiver.get())) {
            // If it's a struct name, this is a static method call - don't bind as variable
            if (!_global_scope->lookupStruct(ident->name)) {
                // Not a struct name, bind as regular expression (instance method)
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

    if (is_intrinsic(call.name)) {
        // Bind arguments for intrinsics
        for (const auto &arg: call.arguments) {
            bindExpression(*arg);
        }
        return;
    }

    // Check for enum construction: Enum::Variant(args)
    const size_t colonPos = call.name.find("::");
    if (colonPos != std::string::npos) {
        const std::string enumName = call.name.substr(0, colonPos);
        const std::string variantName = call.name.substr(colonPos + 2);

        if (const auto enumSym = _global_scope->lookupEnum(enumName)) {
            if (enumSym->hasVariant(variantName)) {
                // Valid enum construction - bind arguments
                const auto *variant = enumSym->getVariant(variantName);
                if (variant && call.arguments.size() != variant->associatedTypes.size()) {
                    errorWrongArgumentCount(call.name, variant->associatedTypes.size(), call.arguments.size(), {});
                }
                for (const auto &arg: call.arguments) {
                    bindExpression(*arg);
                }
                return;
            }
            // Enum exists but variant doesn't
            errorUndefinedFunction(call.name, {});
            return;
        }
    }

    if (const auto funcSym = _global_scope->lookupFunction(call.name); funcSym) {
        if (!funcSym->isVariadic && call.arguments.size() != funcSym->arity()) {
            errorWrongArgumentCount(call.name, funcSym->arity(), call.arguments.size(), {});
        } else if (funcSym->isVariadic && call.arguments.size() < funcSym->arity()) {
            errorWrongArgumentCount(call.name, funcSym->arity(), call.arguments.size(), {});
        }
    } else {
        errorUndefinedFunction(call.name, {});
    }

    for (const auto &arg: call.arguments) {
        bindExpression(*arg);
    }
}