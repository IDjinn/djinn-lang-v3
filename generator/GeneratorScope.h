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

struct GeneratorScope {
    std::shared_ptr<GeneratorScope> parent = nullptr;

    std::unordered_map<std::string, llvm::StructType *> structTypes{};
    std::unordered_map<std::string, std::unordered_map<std::string, unsigned> > structFieldIndices{};
    std::unordered_map<std::string, llvm::AllocaInst *> namedValues{};
    std::unordered_map<std::string, std::string> variableStructTypes{};

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

    [[nodiscard]] bool has_variable_in_current_scope(const std::string &name) const {
        return namedValues.contains(name);
    }
};

#endif //DJINN_GENERATOR_SCOPE_H
