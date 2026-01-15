//
// Created by Luke on 02/01/2026.
//

#ifndef DJINN_MANGLER_H
#define DJINN_MANGLER_H

#include <string>
#include <vector>
#include <sstream>
#include "../parser/ast/Type.h"

class Mangler {
public:
    // _Z + length + name (estilo Itanium C++ ABI simplificado)

    // Função global: _Z3foo -> foo
    static std::string mangle_function(const std::string &name) {
        return "_Z" + std::to_string(name.length()) + name;
    }

    // Função com parâmetros: _Z3fooi -> foo(int)
    static std::string mangle_function(const std::string &name, const std::vector<Type> &params) {
        std::string result = "_Z" + std::to_string(name.length()) + name;
        for (const auto &param: params) {
            result += mangle_type(param);
        }
        return result;
    }

    // Método de struct: _ZN4List4pushEi -> List::push(int)
    static std::string mangle_method(const std::string &structName, const std::string &methodName) {
        return "_ZN" + std::to_string(structName.length()) + structName +
               std::to_string(methodName.length()) + methodName + "E";
    }

    static std::string mangle_method(const std::string &structName, const std::string &methodName,
                                     const std::vector<Type> &params) {
        std::string result = "_ZN" + std::to_string(structName.length()) + structName +
                             std::to_string(methodName.length()) + methodName + "E";
        for (const auto &param: params) {
            result += mangle_type(param);
        }
        return result;
    }

    // Struct genérica: _ZN4ListIiE -> List<int>
    static std::string mangle_generic_struct(const std::string &name, const std::vector<Type> &typeArgs) {
        std::string result = "_ZN" + std::to_string(name.length()) + name + "I";
        for (const auto &arg: typeArgs) {
            result += mangle_type(arg);
        }
        result += "E";
        return result;
    }

    // Enum genérico: _ZE6ResultIiPcE -> Result<int, char*>
    static std::string mangle_generic_enum(const std::string &name, const std::vector<Type> &typeArgs) {
        std::string result = "_ZE" + std::to_string(name.length()) + name + "I";
        for (const auto &arg: typeArgs) {
            result += mangle_type(arg);
        }
        result += "E";
        return result;
    }

    // Método de struct genérica: _ZN4ListIiE4pushEi -> List<int>::push(int)
    static std::string mangle_generic_method(const std::string &structName, const std::vector<Type> &typeArgs,
                                             const std::string &methodName, const std::vector<Type> &params) {
        std::string result = "_ZN" + std::to_string(structName.length()) + structName + "I";
        for (const auto &arg: typeArgs) {
            result += mangle_type(arg);
        }
        result += "E" + std::to_string(methodName.length()) + methodName + "E";
        for (const auto &param: params) {
            result += mangle_type(param);
        }
        return result;
    }

    // Mangle de tipo individual
    static std::string mangle_type(const Type &type) {
        switch (type.kind) {
            case TypeKind::VOID: return "v";
            case TypeKind::INTEGER:
                if (type.sign) {
                    switch (type.size) {
                        case 8: return "a"; // signed char
                        case 16: return "s"; // short
                        case 32: return "i"; // int
                        case 64: return "l"; // long
                        case 128: return "n"; // __int128
                        default: return "i";
                    }
                } else {
                    switch (type.size) {
                        case 8: return "h"; // unsigned char
                        case 16: return "t"; // unsigned short
                        case 32: return "j"; // unsigned int
                        case 64: return "m"; // unsigned long
                        case 128: return "o"; // unsigned __int128
                        default: return "j";
                    }
                }
            case TypeKind::F16: return "Dh"; // half
            case TypeKind::F32: return "f"; // float
            case TypeKind::F64: return "d"; // double
            case TypeKind::F128: return "g"; // __float128
            case TypeKind::STRING: return "Pc"; // char*
            case TypeKind::POINTER:
                return "P" + (type.elementType ? mangle_type(*type.elementType) : "v");
            case TypeKind::ARRAY:
                return "A" + (type.elementType ? mangle_type(*type.elementType) : "v");
            case TypeKind::STRUCT:
                return std::to_string(type.structName.length()) + type.structName;
            default: return "?";
        }
    }

    // Demangle para debug/erros (simplificado)
    static std::string demangle(const std::string &mangled) {
        if (mangled.substr(0, 2) != "_Z") {
            return mangled;
        }
        // Implementação simplificada - retorna o nome básico
        std::string result;
        size_t i = 2;

        if (i < mangled.length() && mangled[i] == 'N') {
            i++; // Pula 'N' para nomes aninhados
            while (i < mangled.length() && mangled[i] != 'E') {
                if (std::isdigit(mangled[i])) {
                    size_t len = 0;
                    while (i < mangled.length() && std::isdigit(mangled[i])) {
                        len = len * 10 + (mangled[i] - '0');
                        i++;
                    }
                    if (!result.empty()) result += "::";
                    result += mangled.substr(i, len);
                    i += len;
                } else if (mangled[i] == 'I') {
                    result += "<";
                    i++;
                    // Simplificado: pula até 'E'
                    while (i < mangled.length() && mangled[i] != 'E') i++;
                    result += "...>";
                    i++;
                } else {
                    i++;
                }
            }
        } else {
            // Função simples
            if (i < mangled.length() && std::isdigit(mangled[i])) {
                size_t len = 0;
                while (i < mangled.length() && std::isdigit(mangled[i])) {
                    len = len * 10 + (mangled[i] - '0');
                    i++;
                }
                result = mangled.substr(i, len);
            }
        }

        return result.empty() ? mangled : result;
    }
};

#endif //DJINN_MANGLER_H