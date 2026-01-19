//
// Created by Claude on 05/01/2026.
//

#include <assert.h>

#include "../Binder.h"

std::unique_ptr<Type> Binder::resolveType(const Type &type) const {
    if (isTypeDefined(type)) {
        return std::make_unique<Type>(type);
    }

    // type name maybe is aliased
    const auto symbol = _current_scope->lookup(type.structName);
    if (!symbol) {
        return nullptr; // Type not found, let caller handle it
    }
    return std::make_unique<Type>(symbol->type);
}

bool Binder::is_generic_type(const Type &type, const StructDeclaration &struc) {
    return !struc.genericParams.empty() && struc.genericParams.find(type.structName) != nullptr;
}

bool Binder::isTypeDefined(Type &type) const {
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

        case TypeKind::STRUCT: {
            if (_global_scope->lookupInterface(type.structName) != nullptr) {
                return true;
            }
            // Check for struct
            if (const auto structSym = _global_scope->lookupStruct(type.structName)) {
                // Normalize: update structName to qualified name if it was an alias
                if (structSym->name != type.structName) {
                    const_cast<Type &>(type).structName = structSym->name;
                }
                // Also normalize generic args
                for (auto &arg: const_cast<Type &>(type).genericArgs) {
                    isTypeDefined(arg);
                }
                return true;
            }
            // Check for enum (enums also use TypeKind::STRUCT)
            if (const auto enumSym = _global_scope->lookupEnum(type.structName)) {
                // Normalize: update structName to qualified name if it was an alias
                if (enumSym->name != type.structName) {
                    const_cast<Type &>(type).structName = enumSym->name;
                }
                // Also normalize generic args
                for (auto &arg: const_cast<Type &>(type).genericArgs) {
                    isTypeDefined(arg);
                }
                return true;
            }
            return false;
        }

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

bool Binder::isTypeDefined(const Type &type) const {
    return isTypeDefined(const_cast<Type &>(type));
}

bool Binder::isTypeDefined(Type *type) const {
    assert(type != nullptr);
    return isTypeDefined(*type);
}

std::optional<Type> Binder::inferExpressionType(const Expression &expr) const {
    if (const auto *intLit = dynamic_cast<const IntegerLiteral *>(&expr)) {
        // Infer size from value magnitude, default to i32/u32
        return Type::integer(32, intLit->sign);
    }

    if (dynamic_cast<const FloatLiteral *>(&expr)) {
        return Type::floating(64); // Default to f64
    }

    if (dynamic_cast<const StringLiteral *>(&expr)) {
        return Type::pointer(Type::integer(8, true)); // i8*
    }

    if (const auto *ident = dynamic_cast<const Identifier *>(&expr)) {
        if (const auto sym = _current_scope->lookupVariable(ident->identifier.token_name)) {
            return sym->type;
        }
    }

    if (const auto *unary = dynamic_cast<const UnaryExpression *>(&expr)) {
        auto operandType = inferExpressionType(*unary->operand);
        if (operandType && unary->op == TokenType::MINUS &&
            operandType->kind == TypeKind::INTEGER) {
            operandType->sign = true;
        }
        return operandType;
    }

    if (const auto *binary = dynamic_cast<const BinaryExpression *>(&expr)) {
        return inferExpressionType(*binary->left);
    }

    if (const auto *call = dynamic_cast<const FunctionCall *>(&expr)) {
        if (const auto funcSym = _global_scope->lookupFunction(call->name.token_name)) {
            return funcSym->type;
        }
    }

    return std::nullopt;
}

void Binder::checkTypeCompatibility(const Type &expected, const Expression &expr, SourceLocation loc) {
    const auto inferredOpt = inferExpressionType(expr);
    if (!inferredOpt)
        return;

    const Type &inferred = *inferredOpt;
    const Type *expectedResolved = &expected;
    if (expected.kind == TypeKind::STRUCT) {
        if (const auto structSym = _global_scope->lookupStruct(expected.structName)) {
            if (structSym->isTransparent() && structSym->baseType) {
                expectedResolved = structSym->baseType.get();
            }
        }
    }

    if (expectedResolved->kind == TypeKind::INTEGER && inferred.kind == TypeKind::INTEGER) {
        if (!expectedResolved->sign && inferred.sign) {
            bool isNegative = false;
            if (const auto *unary = dynamic_cast<const UnaryExpression *>(&expr)) {
                if (unary->op == TokenType::MINUS) {
                    isNegative = true;
                }
            }

            if (isNegative) {
                BINDER_ERROR(DiagnosticCode::TYPE_MISMATCH,
                             "cannot assign negative value to unsigned type",
                             expr, loc);
            }
        }
        // Warning for unsigned -> signed (potential overflow for large values)
        else if (expectedResolved->sign && !inferred.sign) {
            BINDER_WARNING(DiagnosticCode::TYPE_MISMATCH,
                           "implicit conversion from unsigned to signed integer",
                           loc);
        }
    }

    if (expectedResolved->kind == TypeKind::INTEGER &&
        (inferred.kind == TypeKind::F16 || inferred.kind == TypeKind::F32 ||
         inferred.kind == TypeKind::F64 || inferred.kind == TypeKind::F128)) {
        BINDER_ERROR(DiagnosticCode::TYPE_MISMATCH,
                     "cannot implicitly convert floating-point to integer; use explicit cast",
                     expr, loc);
    }
}