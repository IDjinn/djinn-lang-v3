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

struct StructMethodDeclaration;

// Information about a property (C# style getter/setter)
struct PropertyInfo {
    std::string name;
    bool hasGetter = false;
    bool hasSetter = false;
    std::string backingFieldName; // For auto-properties (e.g., "_value")
    unsigned backingFieldIndex = 0;
};

struct GenericStructDef {
    std::string name;
    GenericParams params;
    std::vector<std::pair<std::string, Type> > fields;
    std::vector<const StructMethodDeclaration *> methods;
    std::vector<const StructProperty *> properties; // C# style properties
    llvm::StructType *llvmType = nullptr;
    std::unordered_map<std::string, unsigned> fieldIndices;

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
    std::unordered_map<std::string, std::unordered_map<std::string, unsigned> > structFieldIndices{};
    std::unordered_map<std::string, llvm::Type *> transparentTypes{};

    // Aliases para suportar imports (ex: "c_result" -> "std::types::c_result")
    std::unordered_map<std::string, std::string> structAliases{};
    std::unordered_map<std::string, std::string> transparentAliases{};

    std::unordered_map<std::string, llvm::AllocaInst *> namedValues{};
    std::unordered_map<std::string, std::string> variableStructTypes{};

    std::unordered_map<std::string, std::unordered_map<std::string, llvm::Function *> > structMethods{};
    std::unordered_map<std::string, llvm::Function *> localFunctions{};

    std::unordered_map<std::string, GenericStructDef> genericStructs{};
    std::unordered_map<std::string, GenericFunctionDef> genericFunctions{};

    // Properties: structName -> propertyName -> PropertyInfo
    std::unordered_map<std::string, std::unordered_map<std::string, PropertyInfo> > structProperties{};

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

    void define_struct_alias(const std::string &alias, const std::string &qualifiedName) {
        structAliases[alias] = qualifiedName;
    }

    void define_transparent_alias(const std::string &alias, const std::string &qualifiedName) {
        transparentAliases[alias] = qualifiedName;
    }

    [[nodiscard]] std::string resolve_struct_alias(const std::string &name) const {
        if (const auto it = structAliases.find(name); it != structAliases.end()) {
            return it->second;
        }
        if (parent) {
            return parent->resolve_struct_alias(name);
        }
        return name; // retorna o próprio nome se não houver alias
    }

    [[nodiscard]] std::string resolve_transparent_alias(const std::string &name) const {
        if (const auto it = transparentAliases.find(name); it != transparentAliases.end()) {
            return it->second;
        }
        if (parent) {
            return parent->resolve_transparent_alias(name);
        }
        return name;
    }

    [[nodiscard]] llvm::StructType *lookup_struct(const std::string &name) const {
        if (const auto it = structTypes.find(name); it != structTypes.end()) {
            return it->second;
        }

        const std::string resolved = resolve_struct_alias(name);
        if (resolved != name) {
            if (const auto it = structTypes.find(resolved); it != structTypes.end()) {
                return it->second;
            }
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

        if (const std::string resolved = resolve_struct_alias(name); resolved != name) {
            if (const auto it = structFieldIndices.find(resolved); it != structFieldIndices.end()) {
                return &it->second;
            }
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
        // Tenta resolver alias
        const std::string resolved = resolve_transparent_alias(name);
        if (resolved != name) {
            if (const auto it = transparentTypes.find(resolved); it != transparentTypes.end()) {
                return it->second;
            }
        }
        if (parent) {
            return parent->lookup_transparent_type(name);
        }
        return nullptr;
    }

    [[nodiscard]] bool is_transparent_type(const std::string &name) const {
        if (transparentTypes.contains(name)) return true;
        // Tenta resolver alias
        const std::string resolved = resolve_transparent_alias(name);
        if (resolved != name && transparentTypes.contains(resolved)) return true;
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
        // Primeiro tenta o nome direto
        if (const auto structIt = structMethods.find(structName); structIt != structMethods.end()) {
            if (const auto methodIt = structIt->second.find(methodName); methodIt != structIt->second.end()) {
                return methodIt->second;
            }
        }
        // Depois tenta resolver alias
        const std::string resolved = resolve_struct_alias(structName);
        if (resolved != structName) {
            if (const auto structIt = structMethods.find(resolved); structIt != structMethods.end()) {
                if (const auto methodIt = structIt->second.find(methodName); methodIt != structIt->second.end()) {
                    return methodIt->second;
                }
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
        // Tenta resolver alias
        const std::string resolved = resolve_struct_alias(name);
        if (resolved != name) {
            if (const auto it = genericStructs.find(resolved); it != genericStructs.end()) {
                return &it->second;
            }
        }
        if (parent) {
            return parent->lookup_generic_struct(name);
        }
        return nullptr;
    }

    [[nodiscard]] bool has_generic_struct(const std::string &name) const {
        if (genericStructs.contains(name)) return true;
        const std::string resolved = resolve_struct_alias(name);
        if (resolved != name && genericStructs.contains(resolved)) return true;
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

    // Property methods
    void define_property(const std::string &structName, const PropertyInfo &prop) {
        structProperties[structName][prop.name] = prop;
    }

    [[nodiscard]] const PropertyInfo *
    lookup_property(const std::string &structName, const std::string &propName) const {
        // Try direct lookup
        if (const auto structIt = structProperties.find(structName); structIt != structProperties.end()) {
            if (const auto propIt = structIt->second.find(propName); propIt != structIt->second.end()) {
                return &propIt->second;
            }
        }
        // Try with resolved alias
        const std::string resolved = resolve_struct_alias(structName);
        if (resolved != structName) {
            if (const auto structIt = structProperties.find(resolved); structIt != structProperties.end()) {
                if (const auto propIt = structIt->second.find(propName); propIt != structIt->second.end()) {
                    return &propIt->second;
                }
            }
        }
        if (parent) {
            return parent->lookup_property(structName, propName);
        }
        return nullptr;
    }

    [[nodiscard]] bool has_property(const std::string &structName, const std::string &propName) const {
        return lookup_property(structName, propName) != nullptr;
    }
};

#endif //DJINN_GENERATOR_SCOPE_H