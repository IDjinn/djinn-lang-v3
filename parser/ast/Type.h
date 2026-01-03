//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_TYPE_H
#define DJINN_TYPE_H

#include <string>
#include <memory>
#include <unordered_map>
#include "ASTNode.h"
#include "../../lexer/Token.h"

enum class TypeKind: uint8_t {
    INTEGER,
    STRING,
    VOID,
    F16,
    F32,
    F64,
    F128,
    STRUCT,
    AUTO,
    ARRAY,
    POINTER
};

const std::unordered_map<std::string, TypeKind> string_to_type_kind = {
    {"f16", TypeKind::F16},
    {"f32", TypeKind::F32},
    {"f64", TypeKind::F64},
    {"f128", TypeKind::F128},
    {"void", TypeKind::VOID},
    {"auto", TypeKind::AUTO},
    {"struct", TypeKind::STRUCT},
    {"string", TypeKind::STRING},
};

struct Type : ASTNode {
    size_t size = 0;
    TypeKind kind = TypeKind::VOID;
    bool sign = false;
    bool nullable = false;
    std::unique_ptr<Type> elementType;
    std::string structName;
    std::vector<Type> genericArgs;

    Type() = default;

    Type(const TypeKind kind, const size_t size, const bool sign)
        : size(size),
          kind(kind),
          sign(sign) {
    }

    Type(const Type &other)
        : size(other.size),
          kind(other.kind),
          sign(other.sign),
          nullable(other.nullable),
          elementType(other.elementType ? std::make_unique<Type>(*other.elementType) : nullptr),
          structName(other.structName),
          genericArgs(other.genericArgs) {
    }

    Type &operator=(const Type &other) {
        if (this != &other) {
            kind = other.kind;
            size = other.size;
            sign = other.sign;
            nullable = other.nullable;
            elementType = other.elementType ? std::make_unique<Type>(*other.elementType) : nullptr;
            structName = other.structName;
            genericArgs = other.genericArgs;
        }
        return *this;
    }

    Type(Type &&) = default;

    Type &operator=(Type &&) = default;

    static std::string generate_struct_name() {
        return "__anon_struct_" + std::to_string(rand());
    }

    static Type struct_type(const std::string &name) {
        Type structy(TypeKind::STRUCT, 0, false);
        structy.structName = name;
        return structy;
    }

    static Type generic_struct_type(const std::string &name, std::vector<Type> args) {
        Type structy(TypeKind::STRUCT, 0, false);
        structy.structName = name;
        structy.genericArgs = std::move(args);
        return structy;
    }

    [[nodiscard]] bool hasGenericArgs() const {
        return !genericArgs.empty();
    }

    static Type autod() {
        return Type(TypeKind::AUTO, 0, false);
    }

    static Type floated(const size_t size) {
        switch (size) {
            case static_cast<size_t>(16): return Type{TypeKind::F16, 16, true};
            case static_cast<size_t>(32): return Type{TypeKind::F32, 32, true};
            case static_cast<size_t>(64): return Type{TypeKind::F64, 64, true};
            case static_cast<size_t>(128): return Type{TypeKind::F128, 128, true};
            default: throw std::exception("invalid type bit size kind for float");
        }
    }

    static Type voided() {
        return Type(TypeKind::VOID, 0, false);
    }

    static Type stringed() {
        return Type(TypeKind::STRING, 0, false);
    }

    static Type array(Type elemType) {
        Type arr(TypeKind::ARRAY, 0, false);
        arr.elementType = std::make_unique<Type>(std::move(elemType));
        return arr;
    }

    static Type pointer(Type pointeeType) {
        Type ptr(TypeKind::POINTER, 0, false);
        ptr.elementType = std::make_unique<Type>(std::move(pointeeType));
        return ptr;
    }

    static Type integer(const size_t bits, const bool sign) {
        return Type(TypeKind::INTEGER, bits, sign);
    }

    static std::string kindToString(const TypeKind k) {
        switch (k) {
            case TypeKind::INTEGER: return "INTEGER";
            case TypeKind::STRING: return "STRING";
            case TypeKind::VOID: return "VOID";
            case TypeKind::F16: return "F16";
            case TypeKind::F32: return "F32";
            case TypeKind::F64: return "F64";
            case TypeKind::F128: return "F128";
            case TypeKind::STRUCT: return "STRUCT";
            case TypeKind::AUTO: return "AUTO";
            case TypeKind::ARRAY: return "ARRAY";
            case TypeKind::POINTER: return "POINTER";
            default: return "UNKNOWN";
        }
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "Type(" << kindToString(kind);
        if (kind == TypeKind::STRUCT && !structName.empty()) {
            os << "<" << structName;
            if (!genericArgs.empty()) {
                os << "<";
                for (size_t i = 0; i < genericArgs.size(); ++i) {
                    if (i > 0) os << ", ";
                    genericArgs[i].print(os, 0);
                }
                os << ">";
            }
            os << ">";
        } else if ((kind == TypeKind::ARRAY || kind == TypeKind::POINTER) && elementType) {
            os << "<";
            elementType->print(os, 0);
            os << ">";
        } else if (size > 0) {
            os << ", " << size;
        }
        os << ")";
    }

    static Type fromToken(const Token &token) {
        if (token.value.starts_with("i") || token.value.starts_with("u")) {
            const auto bits = std::stol(token.value.substr(1));
            return Type(TypeKind::INTEGER, bits, token.value.starts_with("i"));
        }

        if (token.value == "f16") return Type(TypeKind::F16, 16, true);
        if (token.value == "f32") return Type(TypeKind::F32, 32, true);
        if (token.value == "f64") return Type(TypeKind::F64, 64, true);
        if (token.value == "f128") return Type(TypeKind::F128, 128, true);
        if (token.value == "void") return Type(TypeKind::VOID, 0, false);
        if (token.value == "string") return Type(TypeKind::STRING, 0, false);
        if (token.value == "auto") return Type(TypeKind::AUTO, 0, false);

        return Type(TypeKind::VOID, 0, false);
    }
};

#endif //DJINN_TYPE_H