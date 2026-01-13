//
// Field access, assignment and brace initializer binding
//

#include "../Binder.h"

void Binder::bindFieldAccess(const FieldAccess &access) {
    bindExpression(*access.object);

    // TODO: Type checking would validate the field exists on the struct type
    // For now, we just bind the expression - field validation happens in type checking
}

void Binder::bindFieldAssignment(const FieldAssignment &assign) {
    bindExpression(*assign.object);

    if (assign.value) {
        bindExpression(*assign.value);
    }

    // TODO: Type checking would validate the field exists and is mutable
}

void Binder::bindBraceInitializer(const BraceInitializer &init, const Type *expectedType) {
    if (expectedType && expectedType->kind == TypeKind::STRUCT) {
        if (const auto structSym = _global_scope->lookupStruct(expectedType->structName)) {
            size_t fieldIndex = 0;
            for (const auto &elem: init.elements) {
                const Type *fieldType = nullptr;

                if (elem.isDesignated()) {
                    if (!structSym->hasMember(elem.fieldName)) {
                        errorUndefinedField(expectedType->structName, elem.fieldName, {});
                    } else {
                        fieldType = structSym->getMemberType(elem.fieldName);
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
                    // Check type compatibility for field initialization
                    if (fieldType) {
                        checkTypeCompatibility(*fieldType, *elem.value, {});
                    }
                }
            }
            return;
        }
    }

    for (const auto &elem: init.elements) {
        if (elem.value) {
            bindExpression(*elem.value);
        }
    }
}