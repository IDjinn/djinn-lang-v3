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
struct StructProperty;

// Information about a property (C# style getter/setter)
struct PropertyInfo {
    std::string name;
    bool hasGetter = false;
    bool hasSetter = false;
    std::string backingFieldName;
    unsigned backingFieldIndex = 0;
};

// Unified struct definition - handles regular, generic, and transparent structs
struct StructDef {
    std::string name;

    // Flags
    bool isGeneric = false;
    bool isTransparent = false;
    bool isMonomorphized = false; // true if this is a specialized version of a generic

    // Generic parameters (only if isGeneric)
    GenericParams genericParams;

    // For transparent types: the underlying type
    llvm::Type *transparentUnderlying = nullptr;

    // Fields: name -> (type, index)
    std::vector<std::pair<std::string, Type> > fields;
    std::unordered_map<std::string, unsigned> fieldIndices;

    // Methods and properties (pointers to AST nodes for deferred generation)
    std::vector<const StructMethodDeclaration *> methods;
    std::vector<const StructProperty *> properties;

    // Properties info (for runtime lookup)
    std::unordered_map<std::string, PropertyInfo> propertyInfos;

    // LLVM type (nullptr until resolved)
    llvm::StructType *llvmType = nullptr;

    // Methods LLVM functions
    std::unordered_map<std::string, llvm::Function *> methodFunctions;

    StructDef() = default;

    StructDef(std::string name, bool isGeneric = false)
        : name(std::move(name)), isGeneric(isGeneric) {
    }

    [[nodiscard]] bool hasField(const std::string &fieldName) const {
        return fieldIndices.contains(fieldName);
    }

    [[nodiscard]] bool hasProperty(const std::string &propName) const {
        return propertyInfos.contains(propName);
    }

    [[nodiscard]] const PropertyInfo *getProperty(const std::string &propName) const {
        if (auto it = propertyInfos.find(propName); it != propertyInfos.end()) {
            return &it->second;
        }
        return nullptr;
    }

    [[nodiscard]] unsigned getFieldIndex(const std::string &fieldName) const {
        if (auto it = fieldIndices.find(fieldName); it != fieldIndices.end()) {
            return it->second;
        }
        return UINT_MAX;
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

    // Unified struct storage: qualifiedName -> StructDef
    std::unordered_map<std::string, StructDef> structs;

    // Aliases for imports (e.g., "c_result" -> "std::types::c_result")
    std::unordered_map<std::string, std::string> structAliases;

    // Variables
    std::unordered_map<std::string, llvm::AllocaInst *> namedValues;
    std::unordered_map<std::string, std::string> variableStructTypes;

    // Functions
    std::unordered_map<std::string, llvm::Function *> localFunctions;
    std::unordered_map<std::string, GenericFunctionDef> genericFunctions;

    explicit GeneratorScope(std::shared_ptr<GeneratorScope> parent = nullptr)
        : parent(std::move(parent)) {
    }

    // ========================================================================
    // Struct operations
    // ========================================================================

    void define_struct(const std::string &name, StructDef def) {
        structs[name] = std::move(def);
    }

    [[nodiscard]] StructDef *lookup_struct(const std::string &name) {
        if (auto it = structs.find(name); it != structs.end()) {
            return &it->second;
        }
        // Try alias
        if (const std::string resolved = resolve_alias(name); resolved != name) {
            if (auto it = structs.find(resolved); it != structs.end()) {
                return &it->second;
            }
        }
        if (parent) {
            return parent->lookup_struct(name);
        }
        return nullptr;
    }

    [[nodiscard]] const StructDef *lookup_struct(const std::string &name) const {
        if (auto it = structs.find(name); it != structs.end()) {
            return &it->second;
        }
        if (const std::string resolved = resolve_alias(name); resolved != name) {
            if (auto it = structs.find(resolved); it != structs.end()) {
                return &it->second;
            }
        }
        if (parent) {
            return parent->lookup_struct(name);
        }
        return nullptr;
    }

    [[nodiscard]] bool has_struct(const std::string &name) const {
        return lookup_struct(name) != nullptr;
    }

    [[nodiscard]] bool has_struct_in_current_scope(const std::string &name) const {
        return structs.contains(name) || structAliases.contains(name);
    }

    [[nodiscard]] StructDef *lookup_monomorphized(const std::string &baseName, const std::vector<Type> &typeArgs) {
        const std::string mangledName = Mangler::mangle_generic_struct(resolve_alias(baseName), typeArgs);
        return lookup_struct(mangledName);
    }

    // ========================================================================
    // Alias operations
    // ========================================================================

    void define_alias(const std::string &alias, const std::string &qualifiedName) {
        structAliases[alias] = qualifiedName;
    }

    [[nodiscard]] std::string resolve_alias(const std::string &name) const {
        if (auto it = structAliases.find(name); it != structAliases.end()) {
            return it->second;
        }
        if (parent) {
            return parent->resolve_alias(name);
        }
        return name;
    }

    // ========================================================================
    // Variable operations
    // ========================================================================

    void define_variable(const std::string &name, llvm::AllocaInst *alloca,
                         const std::string &structTypeName = "") {
        namedValues[name] = alloca;
        if (!structTypeName.empty()) {
            variableStructTypes[name] = structTypeName;
        }
    }

    [[nodiscard]] llvm::AllocaInst *lookup_variable(const std::string &name) const {
        if (auto it = namedValues.find(name); it != namedValues.end()) {
            return it->second;
        }
        if (parent) {
            return parent->lookup_variable(name);
        }
        return nullptr;
    }

    [[nodiscard]] std::string lookup_variable_struct_type(const std::string &name) const {
        if (auto it = variableStructTypes.find(name); it != variableStructTypes.end()) {
            return it->second;
        }
        if (parent) {
            return parent->lookup_variable_struct_type(name);
        }
        return "";
    }

    [[nodiscard]] bool has_variable_in_current_scope(const std::string &name) const {
        return namedValues.contains(name);
    }

    // ========================================================================
    // Function operations
    // ========================================================================

    void define_local_function(const std::string &name, llvm::Function *func) {
        localFunctions[name] = func;
    }

    [[nodiscard]] llvm::Function *lookup_local_function(const std::string &name) const {
        if (auto it = localFunctions.find(name); it != localFunctions.end()) {
            return it->second;
        }
        if (parent) {
            return parent->lookup_local_function(name);
        }
        return nullptr;
    }

    void define_generic_function(const std::string &name, GenericFunctionDef def) {
        genericFunctions[name] = std::move(def);
    }

    [[nodiscard]] const GenericFunctionDef *lookup_generic_function(const std::string &name) const {
        if (auto it = genericFunctions.find(name); it != genericFunctions.end()) {
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

    // ========================================================================
    // Convenience methods (for backward compatibility during refactoring)
    // ========================================================================

    // Get LLVM struct type
    [[nodiscard]] llvm::StructType *get_llvm_struct(const std::string &name) const {
        if (const StructDef *def = lookup_struct(name)) {
            return def->llvmType;
        }
        return nullptr;
    }

    // Get transparent underlying type
    [[nodiscard]] llvm::Type *get_transparent_type(const std::string &name) const {
        if (const StructDef *def = lookup_struct(name)) {
            if (def->isTransparent) {
                return def->transparentUnderlying;
            }
        }
        return nullptr;
    }

    // Check if struct is transparent
    [[nodiscard]] bool is_transparent(const std::string &name) const {
        if (const StructDef *def = lookup_struct(name)) {
            return def->isTransparent;
        }
        return false;
    }

    // Check if struct is generic
    [[nodiscard]] bool is_generic(const std::string &name) const {
        if (const StructDef *def = lookup_struct(name)) {
            return def->isGeneric;
        }
        return false;
    }

    // Get field indices
    [[nodiscard]] const std::unordered_map<std::string, unsigned> *get_field_indices(const std::string &name) const {
        if (const StructDef *def = lookup_struct(name)) {
            return &def->fieldIndices;
        }
        return nullptr;
    }

    // Get property info
    [[nodiscard]] const PropertyInfo *get_property(const std::string &structName, const std::string &propName) const {
        if (const StructDef *def = lookup_struct(structName)) {
            return def->getProperty(propName);
        }
        return nullptr;
    }

    // Get method function
    [[nodiscard]] llvm::Function *get_method(const std::string &structName, const std::string &methodName) const {
        if (const StructDef *def = lookup_struct(structName)) {
            if (auto it = def->methodFunctions.find(methodName); it != def->methodFunctions.end()) {
                return it->second;
            }
        }
        if (parent) {
            return parent->get_method(structName, methodName);
        }
        return nullptr;
    }
};

#endif //DJINN_GENERATOR_SCOPE_H