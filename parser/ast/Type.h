//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_TYPE_H
#define DJINN_TYPE_H

#include <string>
#include <memory>
#include <optional>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include "ASTNode.h"
#include "../../lexer/Token.h"

enum class OverflowMode: uint8_t
{
    None,
    Wrapped,
    Trapped,
    Checked,
    Saturating
};

inline const char* overflow_mode_suffix(const OverflowMode mode)
{
    switch (mode)
    {
    case OverflowMode::Wrapped: return "w";
    case OverflowMode::Trapped: return "t";
    case OverflowMode::Checked: return "c";
    case OverflowMode::Saturating: return "s";
    default: return "";
    }
}

// Native-width types follow the host target (no cross-compilation yet)
inline constexpr size_t native_int_bits = sizeof(void*) * 8;

struct IntegerTypeName
{
    size_t bits;
    bool sign;
    OverflowMode mode;
    bool native = false;
};

// Parses "i32", "u8", "i32w", "nint", "nintt" into components; nullopt for non-integer names
inline std::optional<IntegerTypeName> parse_integer_type_name(const std::string& name)
{
    const auto modeFromChar = [](const char c) -> std::optional<OverflowMode>
    {
        switch (c)
        {
        case 'w': return OverflowMode::Wrapped;
        case 't': return OverflowMode::Trapped;
        case 'c': return OverflowMode::Checked;
        case 's': return OverflowMode::Saturating;
        default: return std::nullopt;
        }
    };

    if (name == "nint")
        return IntegerTypeName{native_int_bits, true, OverflowMode::None, true};
    if (name.length() == 5 && name.starts_with("nint"))
    {
        if (const auto mode = modeFromChar(name[4]))
            return IntegerTypeName{native_int_bits, true, *mode, true};
    }

    if (name.length() < 2) return std::nullopt;
    const char prefix = name[0];
    if (prefix != 'i' && prefix != 'u') return std::nullopt;

    std::string digits = name.substr(1);
    OverflowMode mode = OverflowMode::None;
    if (!digits.empty())
    {
        if (const auto m = modeFromChar(digits.back()))
        {
            mode = *m;
            digits.pop_back();
        }
    }
    if (digits.empty() || !std::ranges::all_of(digits, [](const unsigned char c) { return std::isdigit(c); }))
        return std::nullopt;

    return IntegerTypeName{static_cast<size_t>(std::stol(digits)), prefix == 'i', mode};
}

enum class TypeKind: uint8_t
{
    INTEGER,
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
};

struct Type : Location
{
    size_t size = 0;
    TypeKind kind = TypeKind::VOID;
    bool sign = false;
    bool nullable = false;
    bool readOnly = false;
    bool isTransparent = false; // true for transparent types (struct size : u32) — copy semantics
    bool native = false; // display only: nint/nfloat/ndouble (excluded from identity)
    OverflowMode overflowMode = OverflowMode::None; // behavioral annotation (excluded from identity)
    std::unique_ptr<Type> elementType;
    std::string structName;
    std::vector<Type> genericArgs;

    Type() = default;

    Type(const TypeKind kind, const size_t size, const bool sign)
        : size(size),
          kind(kind),
          sign(sign)
    {
    }

    Type(const Type& other)
        : Location(other),
          size(other.size),
          kind(other.kind),
          sign(other.sign),
          nullable(other.nullable),
          isTransparent(other.isTransparent),
          native(other.native),
          overflowMode(other.overflowMode),
          elementType(other.elementType ? std::make_unique<Type>(*other.elementType) : nullptr),
          structName(other.structName),
          genericArgs(other.genericArgs)
    {
    }

    Type& operator=(const Type& other)
    {
        if (this != &other)
        {
            Location::operator=(other);
            kind = other.kind;
            size = other.size;
            sign = other.sign;
            nullable = other.nullable;
            isTransparent = other.isTransparent;
            native = other.native;
            overflowMode = other.overflowMode;
            elementType = other.elementType ? std::make_unique<Type>(*other.elementType) : nullptr;
            structName = other.structName;
            genericArgs = other.genericArgs;
        }
        return *this;
    }

    bool operator==(const Type& other) const
    {
        if (size != other.size) return false;
        if (kind != other.kind) return false;
        if (sign != other.sign) return false;
        if (nullable != other.nullable) return false;
        if (readOnly != other.readOnly) return false;
        if (structName != other.structName) return false;

        if (elementType && other.elementType)
        {
            if (*elementType != *other.elementType)
                return false;
        }
        else if (elementType || other.elementType)
        {
            return false;
        }

        if (genericArgs != other.genericArgs)
            return false;

        return true;
    }

    bool operator!=(const Type& other) const
    {
        return !(*this == other);
    }

    Type(Type&&) = default;

    Type& operator=(Type&&) = default;

    static std::string generate_struct_name()
    {
        return "__anon_struct_" + std::to_string(rand());
    }

    static Type struct_type(const std::string& name)
    {
        Type structy(TypeKind::STRUCT, 0, false);
        structy.structName = name;
        return structy;
    }

    static Type generic_struct_type(const std::string& name, std::vector<Type> args)
    {
        Type structy(TypeKind::STRUCT, 0, false);
        structy.structName = name;
        structy.genericArgs = std::move(args);
        return structy;
    }

    [[nodiscard]] bool hasGenericArgs() const
    {
        return !genericArgs.empty();
    }

    static Type auto_type()
    {
        return Type(TypeKind::AUTO, 0, false);
    }

    static Type floating(const size_t size)
    {
        switch (size)
        {
        case static_cast<size_t>(16): return Type{TypeKind::F16, 16, true};
        case static_cast<size_t>(32): return Type{TypeKind::F32, 32, true};
        case static_cast<size_t>(64): return Type{TypeKind::F64, 64, true};
        case static_cast<size_t>(128): return Type{TypeKind::F128, 128, true};
        default: throw std::exception("invalid type bit size kind for float");
        }
    }

    static Type voided()
    {
        return Type(TypeKind::VOID, 0, false);
    }

    static Type array(Type elemType)
    {
        Type arr(TypeKind::ARRAY, 0, false);
        arr.elementType = std::make_unique<Type>(std::move(elemType));
        return arr;
    }

    static Type pointer(Type pointeeType)
    {
        Type ptr(TypeKind::POINTER, 0, false);
        ptr.elementType = std::make_unique<Type>(std::move(pointeeType));
        return ptr;
    }

    static Type integer(const size_t bits, const bool sign, const OverflowMode mode = OverflowMode::None)
    {
        Type t(TypeKind::INTEGER, bits, sign);
        t.overflowMode = mode;
        return t;
    }

    static Type native_integer()
    {
        Type t(TypeKind::INTEGER, native_int_bits, true);
        t.native = true;
        return t;
    }

    static Type native_floating(const size_t size)
    {
        Type t = floating(size);
        t.native = true;
        return t;
    }

    static std::string kindToString(const TypeKind k)
    {
        switch (k)
        {
        case TypeKind::INTEGER: return "INTEGER";
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

    [[nodiscard]] std::string toHumanString() const
    {
        switch (kind)
        {
        case TypeKind::INTEGER:
            if (native) return "nint";
            return (sign ? "i" : "u") + std::to_string(size) + overflow_mode_suffix(overflowMode);
        case TypeKind::F16: return "f16";
        case TypeKind::F32: return native ? "nfloat" : "f32";
        case TypeKind::F64: return native ? "ndouble" : "f64";
        case TypeKind::F128: return "f128";
        case TypeKind::VOID: return "void";
        case TypeKind::AUTO: return "auto";
        case TypeKind::POINTER:
            return (elementType ? elementType->toHumanString() : "void") + "*";
        case TypeKind::ARRAY:
            return (elementType ? elementType->toHumanString() : "void") + "[]";
        case TypeKind::STRUCT:
            {
                std::string result = structName;
                if (!genericArgs.empty())
                {
                    result += "<";
                    for (size_t i = 0; i < genericArgs.size(); ++i)
                    {
                        if (i > 0) result += ", ";
                        result += genericArgs[i].toHumanString();
                    }
                    result += ">";
                }
                return result;
            }
        default: return "unknown";
        }
    }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "Type(" << kindToString(kind);
        if (kind == TypeKind::STRUCT && !structName.empty())
        {
            os << "<" << structName;
            if (!genericArgs.empty())
            {
                os << "<";
                for (size_t i = 0; i < genericArgs.size(); ++i)
                {
                    if (i > 0) os << ", ";
                    genericArgs[i].print(os, 0);
                }
                os << ">";
            }
            os << ">";
        }
        else if ((kind == TypeKind::ARRAY || kind == TypeKind::POINTER) && elementType)
        {
            os << "<";
            elementType->print(os, 0);
            os << ">";
        }
        else if (size > 0)
        {
            os << ", " << size;
        }
        if (size > 0)
        {
            os << ", " << (sign ? "signed" : "unsigned");
        }
        os << ")";
    }

    static Type fromToken(const Token& token)
    {
        if (const auto intName = parse_integer_type_name(token.value))
        {
            Type t = intName->native
                         ? native_integer()
                         : Type(TypeKind::INTEGER, intName->bits, intName->sign);
            t.overflowMode = intName->mode;
            return t;
        }

        if (token.value == "nfloat") return Type::native_floating(32);
        if (token.value == "ndouble") return Type::native_floating(64);
        if (token.value == "f16") return Type(TypeKind::F16, 16, true);
        if (token.value == "f32") return Type(TypeKind::F32, 32, true);
        if (token.value == "f64") return Type(TypeKind::F64, 64, true);
        if (token.value == "f128") return Type(TypeKind::F128, 128, true);
        if (token.value == "void") return Type(TypeKind::VOID, 0, false);
        if (token.value == "auto") return Type(TypeKind::AUTO, 0, false);

        return Type(TypeKind::VOID, 0, false);
    }
};

#endif //DJINN_TYPE_H