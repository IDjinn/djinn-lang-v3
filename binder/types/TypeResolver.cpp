//
// Created by Claude on 05/01/2026.
//

#include <cassert>
#include <sstream>

#include "../Binder.h"

std::unique_ptr<Type> Binder::resolveType(const Type& type) const
{
    if (isTypeDefined(type))
    {
        return std::make_unique<Type>(type);
    }

    // type name maybe is aliased
    const auto symbol = _current_scope->lookup(type.structName);
    if (!symbol)
    {
        return nullptr; // Type not found, let caller handle it
    }
    return std::make_unique<Type>(symbol->type);
}

bool Binder::is_generic_type(const Type& type, const StructDeclaration& struc)
{
    return !struc.genericParams.empty() && struc.genericParams.find(type.structName) != nullptr;
}

bool Binder::isTypeDefined(const Type& type) const
{
    // Use mutable reference internally for normalization
    Type& mutableType = const_cast<Type&>(type);

    switch (mutableType.kind)
    {
    case TypeKind::INTEGER:
    case TypeKind::VOID:
    case TypeKind::F16:
    case TypeKind::F32:
    case TypeKind::F64:
    case TypeKind::F128:
    case TypeKind::AUTO:
        return true;

    case TypeKind::STRUCT:
        {
            if (_global_scope->lookupInterface(mutableType.structName) != nullptr)
            {
                return true;
            }
            // Check for struct
            if (const auto structSym = _global_scope->lookupStruct(mutableType.structName))
            {
                // Normalize: update structName to qualified name if it was an alias
                if (structSym->name != mutableType.structName)
                {
                    mutableType.structName = structSym->name;
                }
                // Also normalize generic args
                for (auto& arg : mutableType.genericArgs)
                {
                    isTypeDefined(arg);
                }
                return true;
            }
            // Check for enum (enums also use TypeKind::STRUCT)
            if (const auto enumSym = _global_scope->lookupEnum(mutableType.structName))
            {
                // Normalize: update structName to qualified name if it was an alias
                if (enumSym->name != mutableType.structName)
                {
                    mutableType.structName = enumSym->name;
                }
                // Also normalize generic args
                for (auto& arg : mutableType.genericArgs)
                {
                    isTypeDefined(arg);
                }
                return true;
            }
            return false;
        }

    case TypeKind::ARRAY:
    case TypeKind::POINTER:
        if (mutableType.elementType)
        {
            return isTypeDefined(*mutableType.elementType);
        }
        return false;

    default:
        return false;
    }
}

std::optional<Type> Binder::inferExpressionType(const Expression& expr) const
{
    if (const auto* intLit = dynamic_cast<const IntegerLiteral*>(&expr))
    {
        // Infer size from value magnitude, default to i32/u32
        return Type::integer(32, intLit->sign);
    }

    if (dynamic_cast<const FloatLiteral*>(&expr))
    {
        return Type::floating(64); // Default to f64
    }

    if (dynamic_cast<const StringLiteral*>(&expr))
    {
        return Type::struct_type("str");
    }

    if (dynamic_cast<const NullLiteral*>(&expr))
    {
        Type t = Type::pointer(Type(TypeKind::VOID, 0, false));
        t.nullable = true;
        return t;
    }

    if (const auto* ident = dynamic_cast<const Identifier*>(&expr))
    {
        if (const auto sym = _current_scope->lookupVariable(ident->identifier.token_name))
        {
            return sym->type;
        }
    }

    if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expr))
    {
        auto operandType = inferExpressionType(*unary->operand);
        if (operandType && unary->op == TokenType::MINUS &&
            operandType->kind == TypeKind::INTEGER)
        {
            operandType->sign = true;
        }
        return operandType;
    }

    if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expr)) // TODO: THIS MAY NOT WORK AS EXPECTED
    {
        auto leftType = inferExpressionType(*binary->left);
        // Arithmetic may produce zero (e.g. a - a), so a non-zero operand type
        // does not carry over to the result
        if (leftType && leftType->kind == TypeKind::INTEGER)
        {
            leftType->nonZero = false;
        }
        return leftType;
    }

    if (const auto* call = dynamic_cast<const FunctionCall*>(&expr))
    {
        if (const auto funcSym = _global_scope->lookupFunction(call->name.token_name))
        {
            return funcSym->type;
        }
    }

    if (const auto* fieldAccess = dynamic_cast<const FieldAccess*>(&expr))
    {
        auto objType = inferExpressionType(*fieldAccess->object);
        if (objType)
        {
            std::string structName;
            if (objType->kind == TypeKind::STRUCT) structName = objType->structName;
            else if (objType->kind == TypeKind::POINTER && objType->elementType)
                structName = objType->elementType->structName;
            if (!structName.empty())
            {
                if (const auto structSym = _current_scope->lookupStruct(structName))
                {
                    if (const auto field = structSym->findField(fieldAccess->fieldName.token_name))
                    {
                        return field->type;
                    }
                }
            }
        }
    }

    return std::nullopt;
}

void Binder::checkTypeCompatibility(const Type& expected, const Expression& expr, SourceLocation loc)
{
    // Non-zero types reject the literal 0 in any width (the range check below
    // only covers up to 64 bits)
    if (expected.kind == TypeKind::INTEGER && expected.nonZero && is_zero_literal(expr))
    {
        BINDER_ERROR(DiagnosticCode::TYPE_MISMATCH,
                     "integer literal 0 is not assignable to non-zero type '" + expected.toHumanString() + "'",
                     expr, loc);
    }

    if (dynamic_cast<const NullLiteral*>(&expr))
    {
        if (!expected.nullable)
        {
            BINDER_ERROR(DiagnosticCode::TYPE_MISMATCH,
                         "cannot assign 'null' to non-nullable type — use '" +
                         std::string(expected.kind == TypeKind::STRUCT ? expected.structName : "T") +
                         "?' to allow null",
                         expr, loc);
        }
        return;
    }

    if (const auto* call = dynamic_cast<const FunctionCall*>(&expr))
    {
        if (const auto funcSym = _global_scope->lookupFunction(call->name.token_name))
        {
            if (funcSym->isAsync)
            {
                BINDER_ERROR(DiagnosticCode::TYPE_MISMATCH,
                             "cannot assign result of async function '" + call->name.token_name +
                             "' without 'await'",
                             expr, loc);
            }
        }
    }

    // Integer literal vs integer target: reject out-of-range literals for
    // trapped/checked targets (saturating clamps in the generator, wrapped
    // keeps the silent C-style truncation)
    if (expected.kind == TypeKind::INTEGER && expected.size <= 64)
    {
        const IntegerLiteral* lit = nullptr;
        bool negated = false;
        if (const auto* l = dynamic_cast<const IntegerLiteral*>(&expr))
        {
            lit = l;
        }
        else if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expr))
        {
            if (unary->op == TokenType::MINUS)
            {
                if (const auto* innerLit = dynamic_cast<const IntegerLiteral*>(unary->operand.get()))
                {
                    lit = innerLit;
                    negated = true;
                }
            }
        }

        if (lit && !(negated && !expected.sign))
        {
            uint64_t magnitude = 0;
            bool parseOk = true;
            bool fitsUint64 = true;
            {
                std::string cleaned;
                for (const char c : lit->value)
                {
                    if (c != '_' && c != '\'') cleaned += c;
                }

                int radix = 10;
                std::string digits = cleaned;
                if (cleaned.size() > 2 && cleaned[0] == '0' && (cleaned[1] == 'x' || cleaned[1] == 'X'))
                {
                    radix = 16;
                    digits = cleaned.substr(2);
                }
                else if (cleaned.size() > 2 && cleaned[0] == '0' && (cleaned[1] == 'b' || cleaned[1] == 'B'))
                {
                    radix = 2;
                    digits = cleaned.substr(2);
                }

                for (const char c : digits)
                {
                    int d = -1;
                    if (c >= '0' && c <= '9') d = c - '0';
                    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                    if (d < 0 || d >= radix)
                    {
                        parseOk = false;
                        break;
                    }
                    if (magnitude > (UINT64_MAX - d) / radix)
                    {
                        fitsUint64 = false;
                        break;
                    }
                    magnitude = magnitude * radix + d;
                }
            }

            if (parseOk)
            {
                const unsigned bits = static_cast<unsigned>(expected.size);
                uint64_t limit;
                if (expected.sign)
                {
                    limit = negated
                                ? (1ULL << (bits - 1)) // |INT_MIN|
                                : (bits == 64 ? INT64_MAX : (1ULL << (bits - 1)) - 1);
                }
                else
                {
                    limit = bits == 64 ? UINT64_MAX : (1ULL << bits) - 1;
                }

                if (!fitsUint64 || magnitude > limit)
                {
                    const OverflowMode mode = expected.overflowMode != OverflowMode::None
                                                  ? expected.overflowMode
                                                  : lit->overflowMode;
                    if (mode == OverflowMode::Trapped || mode == OverflowMode::Checked)
                    {
                        BINDER_ERROR(DiagnosticCode::TYPE_MISMATCH,
                                     std::string("integer literal ") + (negated ? "-" : "") + lit->value +
                                     " overflows '" + expected.toHumanString() + "'",
                                     expr, loc);
                    }
                }
            }
        }
    }


    const auto inferredOpt = inferExpressionType(expr);
    if (!inferredOpt)
        return;

    const Type& inferred = *inferredOpt;
    const Type* expectedResolved = &expected;
    if (expected.kind == TypeKind::STRUCT)
    {
        if (const auto structSym = _global_scope->lookupStruct(expected.structName))
        {
            if (structSym->isTransparent() && structSym->baseType)
            {
                expectedResolved = structSym->baseType.get();
            }
        }
    }

    if (expectedResolved->kind == TypeKind::INTEGER && inferred.kind == TypeKind::INTEGER)
    {
        if (expectedResolved->nonZero && !inferred.nonZero)
        {
            // Literals are validated by value above; anything else must prove non-zero-ness
            bool isLiteralValue = dynamic_cast<const IntegerLiteral*>(&expr) != nullptr;
            if (!isLiteralValue)
            {
                if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expr))
                {
                    isLiteralValue = unary->op == TokenType::MINUS
                        && dynamic_cast<const IntegerLiteral*>(unary->operand.get()) != nullptr;
                }
            }
            if (!isLiteralValue)
            {
                BINDER_ERROR(DiagnosticCode::TYPE_MISMATCH,
                             "cannot implicitly convert '" + inferred.toHumanString() +
                             "' to non-zero type '" + expectedResolved->toHumanString() +
                             "'; use an explicit cast",
                             expr, loc);
            }
        }

        if (!expectedResolved->sign && inferred.sign)
        {
            bool isNegative = false;
            if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expr))
            {
                if (unary->op == TokenType::MINUS)
                {
                    isNegative = true;
                }
            }

            if (isNegative)
            {
                BINDER_ERROR(DiagnosticCode::TYPE_MISMATCH,
                             "cannot assign negative value to unsigned type",
                             expr, loc);
            }
        }
        // Warning for unsigned -> signed (potential overflow for large values)
        else if (expectedResolved->sign && !inferred.sign)
        {
            BINDER_WARNING(DiagnosticCode::TYPE_MISMATCH,
                           "implicit conversion from unsigned to signed integer",
                           loc);
        }
    }

    if (expectedResolved->kind == TypeKind::INTEGER &&
        (inferred.kind == TypeKind::F16 || inferred.kind == TypeKind::F32 ||
            inferred.kind == TypeKind::F64 || inferred.kind == TypeKind::F128))
    {
        BINDER_ERROR(DiagnosticCode::TYPE_MISMATCH,
                     "cannot implicitly convert floating-point to integer; use explicit cast",
                     expr, loc);
    }

    // Pointer type compatibility check
    if ((expectedResolved->kind == TypeKind::POINTER || expectedResolved->kind == TypeKind::ARRAY) &&
        (inferred.kind == TypeKind::POINTER || inferred.kind == TypeKind::ARRAY))
    {
        const Type* expectedElem = expectedResolved->elementType.get();
        const Type* inferredElem = inferred.elementType.get();

        if (expectedElem && inferredElem)
        {
            // Allow void* <-> T* (C-style implicit cast, needed for malloc)
            if (expectedElem->kind == TypeKind::VOID || inferredElem->kind == TypeKind::VOID)
            {
                return;
            }

            // Check element type compatibility
            bool compatible = true;
            if (expectedElem->kind != inferredElem->kind)
            {
                compatible = false;
            }
            else if (expectedElem->kind == TypeKind::INTEGER &&
                (expectedElem->size != inferredElem->size || expectedElem->sign != inferredElem->sign
                    || expectedElem->nonZero != inferredElem->nonZero))
            {
                compatible = false;
            }
            else if (expectedElem->kind == TypeKind::STRUCT && expectedElem->structName != inferredElem->structName)
            {
                compatible = false;
            }

            if (!compatible)
            {
                BINDER_ERROR(DiagnosticCode::TYPE_MISMATCH,
                             "incompatible pointer types: cannot assign '" + inferred.toHumanString() +
                             "' to '" + expectedResolved->toHumanString() + "'", expr, loc);
            }
        }
    }
}