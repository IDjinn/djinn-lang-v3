//
// Created by Luke on 02/01/2026.
//

#ifndef DJINN_GENERATOR_SCOPE_H
#define DJINN_GENERATOR_SCOPE_H

#include <memory>
#include <string>
#include <unordered_map>
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "../parser/ast/Generic.h"
#include "Mangler.h"
#include "../binder/Symbol.h"

struct StructMethodDeclaration;
struct StructProperty;

struct PropertyInfo {
    std::string name;
    bool hasGetter = false;
    bool hasSetter = false;
    std::string backingFieldName;
    unsigned backingFieldIndex = 0;
};

struct StructDef {
    std::string name;
    bool isGeneric = false;
    bool isTransparent = false;
    bool isMonomorphized = false;

    GenericParams genericParams;

    llvm::Type *transparentUnderlying = nullptr;

    std::vector<std::pair<std::string, Type> > fields;
    std::unordered_map<std::string, unsigned> fieldIndices;

    std::vector<std::shared_ptr<MethodSymbol> > methods;
    std::vector<std::shared_ptr<PropertySymbol> > properties;
    std::unordered_map<std::string, PropertyInfo> propertyInfos;

    llvm::StructType *llvmType = nullptr;

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
        if (const auto it = propertyInfos.find(propName); it != propertyInfos.end()) {
            return &it->second;
        }
        return nullptr;
    }

    [[nodiscard]] unsigned getFieldIndex(const std::string &fieldName) const {
        if (const auto it = fieldIndices.find(fieldName); it != fieldIndices.end()) {
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

struct EnumVariantDef {
    SourceIdentifier name;
    std::vector<Type> associatedTypes;
    unsigned tag = 0;

    EnumVariantDef() = default;

    EnumVariantDef(std::string name, std::vector<Type> types, unsigned tag)
        : name(std::move(name)), associatedTypes(std::move(types)), tag(tag) {
    }

    [[nodiscard]] bool hasPayload() const { return !associatedTypes.empty(); }
};

struct EnumDef {
    std::string name;
    std::vector<EnumVariantDef> variants;

    llvm::StructType *llvmType = nullptr; // The enum struct type { tag, payload }
    llvm::Type *tagType = nullptr; // Type of the discriminant (i8, i16, etc)
    size_t maxPayloadSize = 0; // Size of largest variant payload

    bool isGeneric = false;
    bool isMonomorphized = false;
    GenericParams genericParams;

    EnumDef() = default;

    explicit EnumDef(std::string name) : name(std::move(name)) {
    }

    EnumDef(std::string name, bool isGeneric) : name(std::move(name)), isGeneric(isGeneric) {
    }

    void addVariant(const std::string &variantName, const std::vector<Type> &types) {
        variants.emplace_back(variantName, types, static_cast<unsigned>(variants.size()));
    }

    [[nodiscard]] bool hasVariant(const std::string &variantName) const {
        for (const auto &v: variants) {
            if (v.name.token_name == variantName) return true;
        }
        return false;
    }

    [[nodiscard]] const EnumVariantDef *getVariant(const std::string &variantName) const {
        for (const auto &v: variants) {
            if (v.name.token_name == variantName) return &v;
        }
        return nullptr;
    }

    [[nodiscard]] unsigned getVariantTag(const std::string &variantName) const {
        for (const auto &v: variants) {
            if (v.name.token_name == variantName) return v.tag;
        }
        return UINT_MAX;
    }

    [[nodiscard]] bool hasPayloadVariants() const {
        for (const auto &v: variants) {
            if (v.hasPayload()) return true;
        }
        return false;
    }
};

struct GeneratorScope {
    std::shared_ptr<GeneratorScope> parent = nullptr;

    std::unordered_map<std::string, StructDef> structs;
    std::unordered_map<std::string, std::string> structAliases;

    std::unordered_map<std::string, llvm::AllocaInst *> namedValues;
    std::unordered_map<std::string, std::string> variableStructTypes;

    std::unordered_map<std::string, llvm::Function *> localFunctions;
    std::unordered_map<std::string, GenericFunctionDef> genericFunctions;

    std::unordered_map<std::string, EnumDef> enums;

    explicit GeneratorScope(std::shared_ptr<GeneratorScope> parent = nullptr)
        : parent(std::move(parent)) {
    }

    void define_struct(const std::string &name, StructDef def) {
        structs[name] = std::move(def);
    }

    [[nodiscard]] StructDef *lookup_struct(const std::string &name) {
        if (const auto it = structs.find(name); it != structs.end()) {
            return &it->second;
        }
        // Try alias
        if (const std::string resolved = resolve_alias(name); resolved != name) {
            if (const auto it = structs.find(resolved); it != structs.end()) {
                return &it->second;
            }
        }
        if (parent) {
            return parent->lookup_struct(name);
        }
        return nullptr;
    }

    [[nodiscard]] const StructDef *lookup_struct(const std::string &name) const {
        if (const auto it = structs.find(name); it != structs.end()) {
            return &it->second;
        }
        if (const std::string resolved = resolve_alias(name); resolved != name) {
            if (const auto it = structs.find(resolved); it != structs.end()) {
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

    void define_enum(const std::string &name, EnumDef def) {
        enums[name] = std::move(def);
    }

    [[nodiscard]] EnumDef *lookup_enum(const std::string &name) {
        if (const auto it = enums.find(name); it != enums.end()) {
            return &it->second;
        }
        if (const std::string resolved = resolve_alias(name); resolved != name) {
            if (const auto it = enums.find(resolved); it != enums.end()) {
                return &it->second;
            }
        }
        if (parent) {
            return parent->lookup_enum(name);
        }
        return nullptr;
    }

    [[nodiscard]] const EnumDef *lookup_enum(const std::string &name) const {
        if (const auto it = enums.find(name); it != enums.end()) {
            return &it->second;
        }
        if (const std::string resolved = resolve_alias(name); resolved != name) {
            if (const auto it = enums.find(resolved); it != enums.end()) {
                return &it->second;
            }
        }
        if (parent) {
            return parent->lookup_enum(name);
        }
        return nullptr;
    }

    [[nodiscard]] bool has_enum(const std::string &name) const {
        return lookup_enum(name) != nullptr;
    }

    [[nodiscard]] EnumDef *lookup_monomorphized_enum(const std::string &baseName, const std::vector<Type> &typeArgs) {
        const std::string mangledName = Mangler::mangle_generic_enum(resolve_alias(baseName), typeArgs);
        return lookup_enum(mangledName);
    }

    void define_alias(const std::string &alias, const std::string &qualifiedName) {
        structAliases[alias] = qualifiedName;
    }

    [[nodiscard]] std::string resolve_alias(const std::string &name) const {
        if (const auto it = structAliases.find(name); it != structAliases.end()) {
            return it->second;
        }
        if (parent) {
            return parent->resolve_alias(name);
        }
        return name;
    }

    void define_variable(const std::string &name, llvm::AllocaInst *alloca,
                         const std::string &structTypeName = "") {
        namedValues[name] = alloca;
        if (!structTypeName.empty()) {
            variableStructTypes[name] = structTypeName;
        }
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

    [[nodiscard]] bool has_variable_in_current_scope(const std::string &name) const {
        return namedValues.contains(name);
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

    [[nodiscard]] llvm::StructType *get_llvm_struct(const std::string &name) const {
        if (const StructDef *def = lookup_struct(name)) {
            return def->llvmType;
        }
        return nullptr;
    }

    [[nodiscard]] llvm::Type *get_transparent_type(const std::string &name) const {
        if (const StructDef *def = lookup_struct(name)) {
            if (def->isTransparent) {
                return def->transparentUnderlying;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool is_transparent(const std::string &name) const {
        if (const StructDef *def = lookup_struct(name)) {
            return def->isTransparent;
        }
        return false;
    }

    [[nodiscard]] bool is_generic(const std::string &name) const {
        if (const StructDef *def = lookup_struct(name)) {
            return def->isGeneric;
        }
        return false;
    }

    [[nodiscard]] const std::unordered_map<std::string, unsigned> *get_field_indices(const std::string &name) const {
        if (const StructDef *def = lookup_struct(name)) {
            return &def->fieldIndices;
        }
        return nullptr;
    }

    [[nodiscard]] const PropertyInfo *get_property(const std::string &structName, const std::string &propName) const {
        if (const StructDef *def = lookup_struct(structName)) {
            return def->getProperty(propName);
        }
        return nullptr;
    }

    [[nodiscard]] llvm::Function *get_method(const std::string &structName, const std::string &methodName) const {
        if (const StructDef *def = lookup_struct(structName)) {
            if (const auto it = def->methodFunctions.find(methodName); it != def->methodFunctions.end()) {
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