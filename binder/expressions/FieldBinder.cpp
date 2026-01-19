//
// Field access, assignment and brace initializer binding
//

#include "../Binder.h"

std::shared_ptr<Symbol> Binder::bindFieldAccess(const FieldAccess &access) {
    return bindExpression(*access.object);

    // TODO: Type checking would validate the field exists on the struct type
    // For now, we just bind the expression - field validation happens in type checking
}

std::shared_ptr<Symbol> Binder::bindFieldAssignment(const FieldAssignment &assign) {
    bindExpression(*assign.object);

    if (assign.value) {
        bindExpression(*assign.value);
    }

    return nullptr;
    // TODO: Type checking would validate the field exists and is mutable
}

std::shared_ptr<Symbol> Binder::bindBraceInitializer(const BraceInitializer &init, const Type *expectedType) {
    if (expectedType && expectedType->kind == TypeKind::STRUCT) {
        const auto structSym = _global_scope->lookupStruct(expectedType->structName);
        if (structSym) {
            size_t fieldIndex = 0;
            for (const auto &elem: init.elements) {
                const Type *fieldType = nullptr;

                if (elem.isDesignated()) {
                    if (!structSym->hasMember(elem.fieldName.token_name)) {
                        BINDER_ERROR(DiagnosticCode::UNDEFINED_FIELD,
                                     "struct '" + expectedType->structName + "' has no field named '" + elem.fieldName.
                                     token_name + "'",
                                     elem, elem.fieldName.location);
                    } else {
                        fieldType = structSym->getMemberType(elem.fieldName.token_name);
                    }
                } else {
                    // Positional initializer - get field by index
                    if (fieldIndex < structSym->fields.size()) {
                        fieldType = &structSym->fields[fieldIndex].type;
                    }
                    fieldIndex++;
                }

                if (elem.value) {
                    bindExpression(*elem.value);
                    if (fieldType) checkTypeCompatibility(*fieldType, *elem.value, {});
                }
            }
            return nullptr;
        }
    }

    for (const auto &elem: init.elements) {
        if (elem.value) {
            bindExpression(*elem.value);
        }
    }

    return nullptr;
}