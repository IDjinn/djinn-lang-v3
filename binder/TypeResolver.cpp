//
// Created by Claude on 05/01/2026.
//

#include "Binder.h"

bool Binder::resolveType(const Type &type) {
    return isTypeDefined(type);
}

bool Binder::is_generic_type(const Type &type, const StructDeclaration &struc) {
    return struc.genericParams.size() > 0 && struc.genericParams.find(type.structName) != nullptr;
}

bool Binder::isTypeDefined(const Type &type) {
    switch (type.kind) {
        case TypeKind::INTEGER:
        case TypeKind::STRING:
        case TypeKind::VOID:
        case TypeKind::F16:
        case TypeKind::F32:
        case TypeKind::F64:
        case TypeKind::F128:
        case TypeKind::AUTO:
            return true;

        case TypeKind::STRUCT:
            // Check both interface and struct - interface takes priority for type usage
            // Struct is only used for instantiation
            if (_global_scope->lookupInterface(type.structName) != nullptr) {
                return true;
            }
            return _global_scope->lookupStruct(type.structName) != nullptr;

        case TypeKind::ARRAY:
        case TypeKind::POINTER:
            if (type.elementType) {
                return isTypeDefined(*type.elementType);
            }
            return false;

        default:
            return false;
    }
}

std::optional<Type> Binder::inferExpressionType(const Expression &expr) const {
    // Integer literals
    if (const auto *intLit = dynamic_cast<const IntegerLiteral *>(&expr)) {
        // Infer size from value magnitude, default to i32/u32
        return Type::integer(32, intLit->sign);
    }

    // Float literals
    if (dynamic_cast<const FloatLiteral *>(&expr)) {
        return Type::floated(64); // Default to f64
    }

    // String literals
    if (dynamic_cast<const StringLiteral *>(&expr)) {
        return Type::pointer(Type::integer(8, true)); // i8*
    }

    // Identifiers - lookup variable type
    if (const auto *ident = dynamic_cast<const Identifier *>(&expr)) {
        if (const auto sym = _current_scope->lookupVariable(ident->name)) {
            return sym->type;
        }
    }

    // Unary expressions
    if (const auto *unary = dynamic_cast<const UnaryExpression *>(&expr)) {
        auto operandType = inferExpressionType(*unary->operand);
        // Unary minus on integer makes it signed (even if literal was unsigned)
        if (operandType && unary->op == TokenType::MINUS &&
            operandType->kind == TypeKind::INTEGER) {
            operandType->sign = true; // Negation implies signed
        }
        return operandType;
    }

    // Binary expressions - use left operand type (simplified)
    if (const auto *binary = dynamic_cast<const BinaryExpression *>(&expr)) {
        return inferExpressionType(*binary->left);
    }

    // Function calls - lookup return type
    if (const auto *call = dynamic_cast<const FunctionCall *>(&expr)) {
        if (const auto funcSym = _global_scope->lookupFunction(call->name)) {
            return funcSym->type;
        }
    }

    return std::nullopt;
}

void Binder::checkTypeCompatibility(const Type &expected, const Expression &expr, SourceLocation loc) {
    auto inferredOpt = inferExpressionType(expr);
    if (!inferredOpt) {
        return; // Can't infer type, skip check
    }

    const Type &inferred = *inferredOpt;

    // Resolve transparent types to their underlying type
    const Type *expectedResolved = &expected;
    if (expected.kind == TypeKind::STRUCT) {
        if (const auto structSym = _global_scope->lookupStruct(expected.structName)) {
            if (structSym->isTransparent() && structSym->baseType) {
                expectedResolved = structSym->baseType.get();
            }
        }
    }

    // Check signed/unsigned mismatch for integers
    if (expectedResolved->kind == TypeKind::INTEGER && inferred.kind == TypeKind::INTEGER) {
        // Only error if assigning negative value to unsigned type
        // Positive signed literals can safely be assigned to unsigned
        if (!expectedResolved->sign && inferred.sign) {
            // Check if expression is a negation (UnaryExpression with MINUS)
            bool isNegative = false;
            if (const auto *unary = dynamic_cast<const UnaryExpression *>(&expr)) {
                if (unary->op == TokenType::MINUS) {
                    isNegative = true;
                }
            }

            if (isNegative) {
                _diagnostics.error(DiagnosticCode::TYPE_MISMATCH,
                                   "cannot assign negative value to unsigned type",
                                   loc);
            }
        }
        // Warning for unsigned -> signed (potential overflow for large values)
        else if (expectedResolved->sign && !inferred.sign) {
            _diagnostics.warning(DiagnosticCode::TYPE_MISMATCH,
                                 "implicit conversion from unsigned to signed integer",
                                 loc);
        }
    }

    // Check float to int conversion
    if (expectedResolved->kind == TypeKind::INTEGER &&
        (inferred.kind == TypeKind::F16 || inferred.kind == TypeKind::F32 ||
         inferred.kind == TypeKind::F64 || inferred.kind == TypeKind::F128)) {
        _diagnostics.warning(DiagnosticCode::TYPE_MISMATCH,
                             "implicit conversion from floating-point to integer may lose precision",
                             loc);
    }
}