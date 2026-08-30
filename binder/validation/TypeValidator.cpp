//
// TypeValidator implementation
//

#include "TypeValidator.h"
#include "../../parser/ast/Expression.h"

namespace djinn::binder
{
    TypeValidator::TypeValidator(DiagnosticEngine& diagnostics,
                                 std::shared_ptr<ScopedSymbolTable> globalScope)
        : _diagnostics(diagnostics),
          _globalScope(std::move(globalScope)),
          _currentScope(_globalScope)
    {
    }

    void TypeValidator::setCurrentScope(std::shared_ptr<ScopedSymbolTable> scope)
    {
        _currentScope = std::move(scope);
    }

    bool TypeValidator::isTypeDefined(Type& type) const
    {
        switch (type.kind)
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
                if (_globalScope->lookupInterface(type.structName) != nullptr)
                {
                    return true;
                }
                // Check for struct
                if (const auto structSym = _globalScope->lookupStruct(type.structName))
                {
                    // Normalize: update structName to qualified name if it was an alias
                    if (structSym->name != type.structName)
                    {
                        type.structName = structSym->name;
                    }
                    // Also normalize generic args
                    for (auto& arg : type.genericArgs)
                    {
                        isTypeDefined(arg);
                    }
                    return true;
                }
                // Check for enum (enums also use TypeKind::STRUCT)
                if (const auto enumSym = _globalScope->lookupEnum(type.structName))
                {
                    // Normalize: update structName to qualified name if it was an alias
                    if (enumSym->name != type.structName)
                    {
                        type.structName = enumSym->name;
                    }
                    // Also normalize generic args
                    for (auto& arg : type.genericArgs)
                    {
                        isTypeDefined(arg);
                    }
                    return true;
                }
                return false;
            }

        case TypeKind::ARRAY:
        case TypeKind::POINTER:
            if (type.elementType)
            {
                return isTypeDefined(*type.elementType);
            }
            return false;

        default:
            return false;
        }
    }

    bool TypeValidator::isTypeDefined(const Type& type) const
    {
        return isTypeDefined(const_cast<Type&>(type));
    }

    void TypeValidator::checkTypeCompatibility(const Type& expected,
                                               const Expression& expr,
                                               SourceLocation loc)
    {
        if (expected.kind == TypeKind::INTEGER && expected.nonZero && is_zero_literal(expr))
        {
            _diagnostics.emitAndPrint(Diagnostic(
                Severity::Error, DiagnosticCode::TYPE_MISMATCH,
                "integer literal 0 is not assignable to non-zero type '" + expected.toHumanString() + "'",
                loc));
        }

        const auto inferredOpt = inferExpressionType(expr);
        if (!inferredOpt) return;

        const Type& inferred = *inferredOpt;
        const Type* expectedResolved = &expected;

        if (expected.kind == TypeKind::STRUCT)
        {
            if (const auto structSym = _globalScope->lookupStruct(expected.structName))
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
                    _diagnostics.emitAndPrint(Diagnostic(
                        Severity::Error, DiagnosticCode::TYPE_MISMATCH,
                        "cannot implicitly convert '" + inferred.toHumanString() +
                        "' to non-zero type '" + expectedResolved->toHumanString() +
                        "'; use an explicit cast", loc));
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
                    _diagnostics.emitAndPrint(Diagnostic(
                        Severity::Error, DiagnosticCode::TYPE_MISMATCH,
                        "cannot assign negative value to unsigned type", loc));
                }
            }
            else if (expectedResolved->sign && !inferred.sign)
            {
                _diagnostics.emitAndPrint(Diagnostic(
                    Severity::Warning, DiagnosticCode::TYPE_MISMATCH,
                    "implicit conversion from unsigned to signed integer", loc));
            }
        }

        if (expectedResolved->kind == TypeKind::INTEGER &&
            (inferred.kind == TypeKind::F16 || inferred.kind == TypeKind::F32 ||
                inferred.kind == TypeKind::F64 || inferred.kind == TypeKind::F128))
        {
            _diagnostics.emitAndPrint(Diagnostic(
                Severity::Error, DiagnosticCode::TYPE_MISMATCH,
                "cannot implicitly convert floating-point to integer; use explicit cast", loc));
        }
    }

    std::optional<Type> TypeValidator::inferExpressionType(const Expression& expr) const
    {
        if (const auto* intLit = dynamic_cast<const IntegerLiteral*>(&expr))
        {
            return Type::integer(32, intLit->sign);
        }

        if (dynamic_cast<const FloatLiteral*>(&expr))
        {
            return Type::floating(64);
        }

        if (dynamic_cast<const StringLiteral*>(&expr))
        {
            return Type::pointer(Type::integer(8, true));
        }

        if (const auto* ident = dynamic_cast<const Identifier*>(&expr))
        {
            if (const auto sym = _currentScope->lookupVariable(ident->identifier.token_name))
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

        if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expr))
        {
            auto leftType = inferExpressionType(*binary->left); // TODO: THIS MAY NOT WORK AS EXPECTED
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
            if (const auto funcSym = _globalScope->lookupFunction(call->name.token_name))
            {
                return funcSym->type;
            }
        }

        return std::nullopt;
    }

    std::unique_ptr<Type> TypeValidator::resolveType(const Type& type) const
    {
        if (isTypeDefined(type))
        {
            return std::make_unique<Type>(type);
        }

        // type name maybe is aliased
        const auto symbol = _currentScope->lookup(type.structName);
        if (!symbol)
        {
            return nullptr;
        }
        return std::make_unique<Type>(symbol->type);
    }

    bool TypeValidator::isGenericType(const Type& type, const StructDeclaration& struc)
    {
        return !struc.genericParams.empty() && struc.genericParams.find(type.structName) != nullptr;
    }
} // namespace djinn::binder