//
// Field access, assignment and brace initializer binding
//

#include <memory>

#include "../Binder.h"

std::shared_ptr<Symbol> Binder::bindFieldAccess(const FieldAccess& access)
{
    const auto object_symbol = bindExpression(*access.object);

    std::string structName;
    if (object_symbol->type.kind == TypeKind::POINTER)
    {
        structName = object_symbol->type.elementType->structName;
    }
    else if (object_symbol->type.kind == TypeKind::STRUCT)
    {
        structName = object_symbol->type.structName;
    }
    else if (object_symbol->type.kind == TypeKind::ARRAY)
    {
        // Arrays use the arr<T> backing struct
        structName = "arr";
    }

    if (!structName.empty())
    {
        const auto object_structure = _current_scope->lookupStruct(structName);
        if (!object_structure)
        {
            BINDER_ERROR(
                DiagnosticCode::UNDEFINED_STRUCT,
                "struct with name '" + structName + "' could not be located!",
                object_symbol,
                object_symbol->location
            );
        }

        const auto field = object_structure->findField(access.fieldName.token_name);
        if (!field)
        {
            BINDER_ERROR(
                DiagnosticCode::UNDEFINED_FIELD,
                "field with name '" + access.fieldName.token_name + "' could not be located inside struct '" +
                structName +
                "'!",
                object_symbol,
                object_symbol->location
            );
        }

        return std::make_shared<Symbol>(field.value());
    }

    return object_symbol;
}

std::shared_ptr<Symbol> Binder::bindFieldAssignment(const FieldAssignment& assign)
{
    bindExpression(*assign.object);

    if (assign.value)
    {
        bindExpression(*assign.value);
    }

    return nullptr;
    // TODO: Type checking would validate the field exists and is mutable
}

std::shared_ptr<Symbol> Binder::bindBraceInitializer(const BraceInitializer& init, const Type* expectedType)
{
    if (expectedType && expectedType->kind == TypeKind::STRUCT)
    {
        const auto structSym = _global_scope->lookupStruct(expectedType->structName);
        if (structSym)
        {
            size_t fieldIndex = 0;
            for (const auto& elem : init.elements)
            {
                const Type* fieldType = nullptr;

                if (elem.isDesignated())
                {
                    if (!structSym->hasMember(elem.fieldName.token_name))
                    {
                        BINDER_ERROR(DiagnosticCode::UNDEFINED_FIELD,
                                     "struct '" + expectedType->structName + "' has no field named '" + elem.fieldName.
                                     token_name + "'",
                                     elem, elem.fieldName.location);
                    }
                    else
                    {
                        fieldType = structSym->getMemberType(elem.fieldName.token_name);
                    }
                }
                else
                {
                    // Positional initializer - get field by index
                    if (fieldIndex < structSym->fields.size())
                    {
                        fieldType = &structSym->fields[fieldIndex].type;
                    }
                    fieldIndex++;
                }

                if (elem.value)
                {
                    bindExpression(*elem.value);
                    if (fieldType) checkTypeCompatibility(*fieldType, *elem.value, elem.value->location);
                }
            }
            return nullptr;
        }
    }

    for (const auto& elem : init.elements)
    {
        if (elem.value)
        {
            bindExpression(*elem.value);
        }
    }

    return nullptr;
}

std::shared_ptr<Symbol> Binder::bindIndexAccess(const IndexAccess& expr)
{
    auto object = bindExpression(*expr.object);
    bindExpression(*expr.index);

    // Index access on pointer/array dereferences to the element type
    if (object && (object->type.kind == TypeKind::POINTER || object->type.kind == TypeKind::ARRAY)
        && object->type.elementType)
    {
        return std::make_shared<Symbol>(
            SymbolKind::Variable, object->name, *object->type.elementType, expr.location);
    }

    return object;
}