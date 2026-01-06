//
// Created by Luke on 02/01/2026.
//

#ifndef DJINN_GENERATOR_SCOPE_H
#define DJINN_GENERATOR_SCOPE_H

#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "../parser/ast/Generic.h"
#include "Mangler.h"

struct GenericStructDef {
    std::string name;
    GenericParams params;
    std::vector<std::pair<std::string, Type> > fields;

    GenericStructDef() = default;

    GenericStructDef(std::string name, GenericParams params)
        : name(std::move(name)), params(std::move(params)) {
    }
};

struct GenericFunctionDef {
    std::string name;
    GenericParams params;
    Type returnType = Type::voided();
    std::vector<std::pair<std::string, Type> > parameters;

    GenericFunctionDef() = default;

    GenericFunctionDef(std::string name, GenericParams params, Type returnType)
        : name(std::move(name)), params(std::move(params)), returnType(std::move(returnType)) {
    }
};

struct GeneratorScope {
    std::shared_ptr<GeneratorScope> parent = nullptr;

    std::unordered_map<std::string, llvm::StructType *> structTypes{};
    std::unordered_map<std::string, std::unordered_map<std::string, unsigned>> structFieldIndices{};
    std::unordered_map<std::string, llvm::Type *> transparentTypes{};

    std::unordered_map<std::string, llvm::AllocaInst *> namedValues{};
    std::unordered_map<std::string, std::string> variableStructTypes{};

    std::unordered_map<std::string, std::unordered_map<std::string, llvm::Function *>> structMethods{};
    std::unordered_map<std::string, llvm::Function *> localFunctions{};

    std::unordered_map<std::string, GenericStructDef> genericStructs{};
    std::unordered_map<std::string, GenericFunctionDef> genericFunctions{};

    explicit GeneratorScope(std::shared_ptr<GeneratorScope> parent = nullptr)
        : parent(std::move(parent)) {
    }

    void define_struct(const std::string &name, llvm::StructType *type,
                       std::unordered_map<std::string, unsigned> fieldIndices) {
        structTypes[name] = type;
        structFieldIndices[name] = std::move(fieldIndices);
    }

    void define_variable(const std::string &name, llvm::AllocaInst *alloca,
                         const std::string &structTypeName = "") {
        namedValues[name] = alloca;
        if (!structTypeName.empty()) {
            variableStructTypes[name] = structTypeName;
        }
    }

    [[nodiscard]] llvm::StructType *lookup_struct(const std::string &name) const {
        if (const auto it = structTypes.find(name); it != structTypes.end()) {
            return it->second;
        }
        if (parent) {
            return parent->lookup_struct(name);
        }
        return nullptr;
    }

    [[nodiscard]] const std::unordered_map<std::string, unsigned> *lookup_field_indices(const std::string &name) const {
        if (const auto it = structFieldIndices.find(name); it != structFieldIndices.end()) {
            return &it->second;
        }
        if (parent) {
            return parent->lookup_field_indices(name);
        }
        return nullptr;
    }

    [[nodiscard]] llvm::AllocaInst *lookup_variable(const std::string &name) const {
        if (const auto it = namedValues.find(name); it != namedValues.end()) {
            return it->second;
        }
        if (parent) {
            return parent->lookup_variable(name);
        }
        return nullptr;
    }

    [[nodiscard]] std::string lookup_variable_struct_type(const std::string &name) const {
        if (const auto it = variableStructTypes.find(name); it != variableStructTypes.end()) {
            return it->second;
        }
        if (parent) {
            return parent->lookup_variable_struct_type(name);
        }
        return "";
    }

    [[nodiscard]] bool has_struct_in_current_scope(const std::string &name) const {
        return structTypes.contains(name);
    }

    void define_transparent_type(const std::string &name, llvm::Type *underlyingType) {
        transparentTypes[name] = underlyingType;
    }

    [[nodiscard]] llvm::Type *lookup_transparent_type(const std::string &name) const {
        if (const auto it = transparentTypes.find(name); it != transparentTypes.end()) {
            return it->second;
        }
        if (parent) {
            return parent->lookup_transparent_type(name);
        }
        return nullptr;
    }

    [[nodiscard]] bool is_transparent_type(const std::string &name) const {
        if (transparentTypes.contains(name)) return true;
        if (parent) return parent->is_transparent_type(name);
        return false;
    }

    [[nodiscard]] bool has_variable_in_current_scope(const std::string &name) const {
        return namedValues.contains(name);
    }

    void define_method(const std::string &structName, const std::string &methodName, llvm::Function *func) {
        structMethods[structName][methodName] = func;
    }

    [[nodiscard]] llvm::Function *lookup_method(const std::string &structName, const std::string &methodName) const {
        if (const auto structIt = structMethods.find(structName); structIt != structMethods.end()) {
            if (const auto methodIt = structIt->second.find(methodName); methodIt != structIt->second.end()) {
                return methodIt->second;
            }
        }
        if (parent) {
            return parent->lookup_method(structName, methodName);
        }
        return nullptr;
    }

    [[nodiscard]] bool has_method_in_current_scope(const std::string &structName, const std::string &methodName) const {
        if (const auto structIt = structMethods.find(structName); structIt != structMethods.end()) {
            return structIt->second.contains(methodName);
        }
        return false;
    }

    void define_local_function(const std::string &name, llvm::Function *func) {
        localFunctions[name] = func;
    }

    [[nodiscard]] llvm::Function *lookup_local_function(const std::string &name) const {
        if (const auto it = localFunctions.find(name); it != localFunctions.end()) {
            return it->second;
        }
        if (parent) {
            return parent->lookup_local_function(name);
        }
        return nullptr;
    }

    [[nodiscard]] bool has_local_function_in_current_scope(const std::string &name) const {
        return localFunctions.contains(name);
    }

    void define_generic_struct(const std::string &name, GenericStructDef def) {
        genericStructs[name] = std::move(def);
    }

    [[nodiscard]] const GenericStructDef *lookup_generic_struct(const std::string &name) const {
        if (const auto it = genericStructs.find(name); it != genericStructs.end()) {
            return &it->second;
        }
        if (parent) {
            return parent->lookup_generic_struct(name);
        }
        return nullptr;
    }

    [[nodiscard]] bool has_generic_struct(const std::string &name) const {
        if (genericStructs.contains(name)) return true;
        if (parent) return parent->has_generic_struct(name);
        return false;
    }

    [[nodiscard]] llvm::StructType *lookup_monomorphized_struct(const std::string &baseName,
                                                                const std::vector<Type> &typeArgs) const {
        const std::string mangledName = Mangler::mangle_generic_struct(baseName, typeArgs);
        return lookup_struct(mangledName);
    }

    void define_generic_function(const std::string &name, GenericFunctionDef def) {
        genericFunctions[name] = std::move(def);
    }

    [[nodiscard]] const GenericFunctionDef *lookup_generic_function(const std::string &name) const {
        if (const auto it = genericFunctions.find(name); it != genericFunctions.end()) {
            return &it->second;
        }
        if (parent) {
            return parent->lookup_generic_function(name);
        }
        return nullptr;
    }

    [[nodiscard]] bool has_generic_function(const std::string &name) const {
        if (genericFunctions.contains(name)) return true;
        if (parent) return parent->has_generic_function(name);
        return false;
    }

    void define_monomorphized_struct(const std::string &baseName, const std::vector<Type> &typeArgs,
                                     llvm::StructType *type,
                                     std::unordered_map<std::string, unsigned> fieldIndices) {
        const std::string mangledName = Mangler::mangle_generic_struct(baseName, typeArgs);
        structTypes[mangledName] = type;
        structFieldIndices[mangledName] = std::move(fieldIndices);
    }

    void define_mangled_method(const std::string &structName, const std::string &methodName,
                               const std::vector<Type> &paramTypes, llvm::Function *func) {
        const std::string mangledName = Mangler::mangle_method(structName, methodName, paramTypes);
        structMethods[structName][mangledName] = func;
    }

    void define_mangled_function(const std::string &name, const std::vector<Type> &paramTypes,
                                 llvm::Function *func) {
        const std::string mangledName = Mangler::mangle_function(name, paramTypes);
        localFunctions[mangledName] = func;
    }
};

#endif //DJINN_GENERATOR_SCOPE_H
