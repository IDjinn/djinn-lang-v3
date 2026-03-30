//
// Binary and unary operator binding
//

#include "../Binder.h"

std::shared_ptr<Symbol> Binder::bindBinaryExpression(const BinaryExpression& expr)
{
    auto location = expr.location;
    auto left = bindExpression(*expr.left);
    auto right = bindExpression(*expr.right);
    assert(left && right && "Return of binary expression should be valid pointers!");

    // Resolve transparent types (e.g., size : u32 -> u32) for type checking
    // Also handles POINTER types by resolving the element type
    auto resolveTransparent = [this](const Type& type) -> Type
    {
        // For pointer types, resolve the pointed-to type
        if (type.kind == TypeKind::POINTER && type.elementType)
        {
            if (type.elementType->kind == TypeKind::STRUCT)
            {
                if (const auto structSym = _global_scope->lookupStruct(type.elementType->structName))
                {
                    if (structSym->isTransparent() && structSym->baseType)
                    {
                        return Type::pointer(*structSym->baseType);
                    }
                }
            }
            return type;
        }

        if (type.kind == TypeKind::STRUCT)
        {
            if (const auto structSym = _global_scope->lookupStruct(type.structName))
            {
                if (structSym->isTransparent() && structSym->baseType)
                {
                    return *structSym->baseType;
                }
            }
        }
        return type;
    };

    Type leftResolved = resolveTransparent(left->type);
    Type rightResolved = resolveTransparent(right->type);

    // Check if a STRUCT type is a generic param (not registered as struct/enum)
    auto isGenericParam = [this](const Type& type) -> bool // TODO: VALID FUNCTION FOR TYPE GENERIC CHECK
    {
        if (type.kind != TypeKind::STRUCT) return false;
        if (type.structName.empty()) return false;
        if (_global_scope->lookupStruct(type.structName)) return false;
        if (_global_scope->lookupEnum(type.structName)) return false;
        return true; // not found = generic type parameter
    };

    bool hasOperatorOverride = false;
    // Structs (non-transparent) cannot be used directly in binary expressions
    // unless they have an operator overload for that operation.
    // Skip check for generic type params (validated at monomorphization)
    if ((leftResolved.kind == TypeKind::STRUCT || rightResolved.kind == TypeKind::STRUCT)
        && !isGenericParam(leftResolved) && !isGenericParam(rightResolved))
    {
        const auto& structType = leftResolved.kind == TypeKind::STRUCT ? leftResolved : rightResolved;
        if (const auto structSym = _global_scope->lookupStruct(structType.structName))
        {
            // Look for operator overload by canonical name
            auto findOperator = [&](const std::string& canonicalName) -> bool
            {
                for (const auto& m : structSym->methods)
                {
                    if (m->isOperator && m->operatorCanonicalName == canonicalName) return true;
                    // Legacy support: equals method counts as __op_eq
                    if (canonicalName == "__op_eq" && m->name == "equals") return true;
                }
                return false;
            };

            std::string opCanonical;
            switch (expr.op)
            {
            case TokenType::PLUS: opCanonical = "__op_add";
                break;
            case TokenType::MINUS: opCanonical = "__op_sub";
                break;
            case TokenType::STAR: opCanonical = "__op_mul";
                break;
            case TokenType::SLASH: opCanonical = "__op_div";
                break;
            case TokenType::PERCENT: opCanonical = "__op_mod";
                break;
            case TokenType::EQUAL_EQUAL: opCanonical = "__op_eq";
                break;
            case TokenType::BANG_EQUAL: opCanonical = "__op_eq";
                break; // != derived from ==
            case TokenType::LESS: opCanonical = "__op_lt";
                break;
            case TokenType::LESS_EQUAL: opCanonical = "__op_lte";
                break;
            case TokenType::GREATER: opCanonical = "__op_gt";
                break;
            case TokenType::GREATER_EQUAL: opCanonical = "__op_gte";
                break;
            case TokenType::AMPERSAND: opCanonical = "__op_bitand";
                break;
            case TokenType::PIPE: opCanonical = "__op_bitor";
                break;
            case TokenType::CARET: opCanonical = "__op_bitxor";
                break;
            case TokenType::LESS_LESS: opCanonical = "__op_shl";
                break;
            case TokenType::GREATER_GREATER: opCanonical = "__op_shr";
                break;
            case TokenType::AND_AND: opCanonical = "__op_and";
                break;
            case TokenType::OR_OR: opCanonical = "__op_or";
                break;
            default: break;
            }

            if (!opCanonical.empty() && findOperator(opCanonical))
            {
                hasOperatorOverride = true;
            }
            else if (!opCanonical.empty())
            {
                BINDER_ERROR(
                    DiagnosticCode::TYPE_MISMATCH,
                    "invalid types for binary expression: struct '" + structType.structName +
                    "' does not implement operator '" + opCanonical + "'"
                    "\n\tleft: " + left->type.toHumanString() +
                    "\n\tright: " + right->type.toHumanString(),
                    left,
                    location
                );
            }
        }
    }

    // Check if either side involves a generic type (including pointers to generics)
    auto involvesGeneric = [&isGenericParam](const Type& type) -> bool // TODO: VALID FUNCTION FOR TYPE GENERIC CHECK
    {
        if (isGenericParam(type)) return true;
        if (type.kind == TypeKind::POINTER && type.elementType && isGenericParam(*type.elementType)) return true;
        return false;
    };

    const bool hasGeneric = involvesGeneric(leftResolved) || involvesGeneric(rightResolved);

    // Type mismatch check (allow integer width differences - generator handles widening)
    // Skip for generic types (validated at monomorphization)
    if (leftResolved != rightResolved && !hasGeneric && !hasOperatorOverride)
    {
        const bool bothIntegers = leftResolved.kind == TypeKind::INTEGER && rightResolved.kind == TypeKind::INTEGER;
        const bool bothFloats =
            (leftResolved.kind == TypeKind::F32 || leftResolved.kind == TypeKind::F64) &&
            (rightResolved.kind == TypeKind::F32 || rightResolved.kind == TypeKind::F64);

        // Pointer arithmetic: ptr + int or int + ptr (for +, -)
        const bool isPointerArithmetic =
            (expr.op == TokenType::PLUS || expr.op == TokenType::MINUS) &&
            ((leftResolved.kind == TypeKind::POINTER && rightResolved.kind == TypeKind::INTEGER) ||
                (leftResolved.kind == TypeKind::INTEGER && rightResolved.kind == TypeKind::POINTER));

        if (!bothIntegers && !bothFloats && !isPointerArithmetic)
        {
            BINDER_ERROR(
                DiagnosticCode::TYPE_MISMATCH,
                "invalid types for binary expression:"
                "\n\tleft: " + left->type.toHumanString() +
                "\n\tright: " + right->type.toHumanString(),
                left,
                location
            );
        }

        // Check integer narrowing: larger type cannot implicitly fit in smaller
        if (bothIntegers && leftResolved.size != rightResolved.size)
        {
            const auto& larger = leftResolved.size > rightResolved.size ? leftResolved : rightResolved;
            const auto& smaller = leftResolved.size > rightResolved.size ? rightResolved : leftResolved;

            BINDER_WARNING(
                DiagnosticCode::TYPE_MISMATCH,
                "implicit integer widening in binary expression: " +
                smaller.toHumanString() + " -> " + larger.toHumanString() +
                "; consider explicit cast",
                location
            );
        }

        // Check sign mismatch (e.g., i32 vs u32)
        if (bothIntegers && leftResolved.size == rightResolved.size && leftResolved.sign != rightResolved.sign)
        {
            BINDER_WARNING(
                DiagnosticCode::TYPE_MISMATCH,
                "signed/unsigned mismatch in binary expression: " +
                leftResolved.toHumanString() + " vs " + rightResolved.toHumanString(),
                location
            );
        }
    }

    // Determine result type: comparison/logical ops return bool, arithmetic returns operand type
    Type resultType;
    switch (expr.op)
    {
    case TokenType::EQUAL_EQUAL:
    case TokenType::BANG_EQUAL:
    case TokenType::LESS:
    case TokenType::LESS_EQUAL:
    case TokenType::GREATER:
    case TokenType::GREATER_EQUAL:
    case TokenType::AND_AND:
    case TokenType::OR_OR:
        resultType = Type::integer(1, false); // bool (i1)
        break;
    default:
        resultType = leftResolved;
        break;
    }

    return std::make_shared<BinaryExpressionSymbol>(
        tokenTypeToString(expr.op), std::move(left), std::move(right), resultType, location
    );
}

std::shared_ptr<Symbol> Binder::bindUnaryExpression(const UnaryExpression& expr)
{
    auto operand = bindExpression(*expr.operand);

    Type resultType = operand ? operand->type : Type::voided();

    return std::make_shared<UnaryExpressionSymbol>(
        tokenTypeToString(expr.op), std::move(operand), resultType, expr.location
    );
}