//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_SCOPE_H
#define DJINN_SCOPE_H

#include <string>
#include <memory>
#include <unordered_map>
#include "Type.h"

struct Scope {
    std::shared_ptr<Scope> parent = nullptr;

    std::unordered_map<std::string, std::shared_ptr<Type> > structs{};
    std::unordered_map<std::string, std::shared_ptr<Type> > variables{};

    explicit Scope(std::shared_ptr<Scope> parent = nullptr) : parent(std::move(parent)) {
    }

    void define_variable(const std::string &name, const Type &type) {
        variables[name] = std::make_shared<Type>(type);
    }

    void define_struct(const std::string &name, const Type &type) {
        structs[name] = std::make_shared<Type>(type);
    }

    std::shared_ptr<Type> lookup_variable(const std::string &name) {
        if (const auto it = variables.find(name); it != variables.end()) {
            return it->second;
        }
        if (parent) {
            return parent->lookup_variable(name);
        }
        return nullptr;
    }

    std::shared_ptr<Type> lookup_struct(const std::string &name) {
        if (const auto it = structs.find(name); it != structs.end()) {
            return it->second;
        }
        if (parent) {
            return parent->lookup_struct(name);
        }
        return nullptr;
    }

    [[nodiscard]] bool has_variable_in_current_scope(const std::string &name) const {
        return variables.contains(name);
    }

    [[nodiscard]] bool has_struct_in_current_scope(const std::string &name) const {
        return structs.contains(name);
    }

    [[nodiscard]] bool has_struct_declared(const std::string &name) const {
        if (structs.contains(name)) return true;

        if (parent) {
            return parent->has_struct_declared(name);
        }

        return false;
    }
};

#endif //DJINN_SCOPE_H