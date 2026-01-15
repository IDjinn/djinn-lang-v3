//
// Created by Luke on 02/01/2026.
//

#ifndef DJINN_GENERIC_H
#define DJINN_GENERIC_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "Type.h"
#include "ASTNode.h"

struct GenericParam : Location {
    SourceIdentifier name;
    std::unique_ptr<Type> constraint;
    std::unique_ptr<Type> defaultType;

    explicit GenericParam(SourceIdentifier name)
        : name(std::move(name)) {
    }

    GenericParam(SourceIdentifier name, std::unique_ptr<Type> defaultType)
        : name(std::move(name)), defaultType(std::move(defaultType)) {
    }

    GenericParam(const GenericParam &other)
        : Location(other),
          name(other.name),
          constraint(other.constraint ? std::make_unique<Type>(*other.constraint) : nullptr),
          defaultType(other.defaultType ? std::make_unique<Type>(*other.defaultType) : nullptr) {
    }

    GenericParam &operator=(const GenericParam &other) {
        if (this != &other) {
            Location::operator=(other);
            name = other.name;
            constraint = other.constraint ? std::make_unique<Type>(*other.constraint) : nullptr;
            defaultType = other.defaultType ? std::make_unique<Type>(*other.defaultType) : nullptr;
        }
        return *this;
    }

    GenericParam(GenericParam &&) = default;

    GenericParam &operator=(GenericParam &&) = default;

    void print(std::ostream &os, int indent = 0) const override {
        writeIndent(os, indent);
        os << "GenericParam(" << name;
        if (defaultType) {
            os << " = ";
            defaultType->print(os, 0);
        }
        os << ")";
    }
};

struct GenericParams : Location {
    std::vector<GenericParam> params;

    GenericParams() = default;

    void add(GenericParam param) {
        params.push_back(std::move(param));
    }

    [[nodiscard]] bool empty() const { return params.empty(); }
    [[nodiscard]] size_t size() const { return params.size(); }

    [[nodiscard]] const GenericParam *find(const std::string &name) const {
        for (const auto &p: params) {
            if (p.name.token_name == name) return &p;
        }
        return nullptr;
    }

    void print(std::ostream &os, int indent = 0) const override {
        writeIndent(os, indent);
        os << "<";
        for (size_t i = 0; i < params.size(); ++i) {
            if (i > 0) os << ", ";
            params[i].print(os, 0);
        }
        os << ">";
    }
};

struct GenericArgs : Location {
    std::vector<Type> args;

    GenericArgs() = default;

    void add(Type type) {
        args.push_back(std::move(type));
    }

    [[nodiscard]] bool empty() const { return args.empty(); }
    [[nodiscard]] size_t size() const { return args.size(); }

    void print(std::ostream &os, int indent = 0) const override {
        writeIndent(os, indent);
        os << "<";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) os << ", ";
            args[i].print(os, 0);
        }
        os << ">";
    }
};

struct GenericContext {
    std::unordered_map<std::string, Type> substitutions;

    void bind(const std::string &param, const Type &type) {
        substitutions[param] = type;
    }

    [[nodiscard]] const Type *resolve(const std::string &param) const {
        if (const auto it = substitutions.find(param); it != substitutions.end()) {
            return &it->second;
        }
        return nullptr;
    }

    [[nodiscard]] Type substitute(const Type &type) const {
        if (type.kind == TypeKind::STRUCT) {
            if (const auto *resolved = resolve(type.structName)) {
                return *resolved;
            }
        }

        Type result = type;
        if (type.elementType) {
            result.elementType = std::make_unique<Type>(substitute(*type.elementType));
        }
        return result;
    }

    static GenericContext create(const GenericParams &params, const GenericArgs &args) {
        GenericContext ctx;
        for (size_t i = 0; i < params.size(); ++i) {
            if (i < args.size()) {
                ctx.bind(params.params[i].name.token_name, args.args[i]);
            } else if (params.params[i].defaultType) {
                ctx.bind(params.params[i].name.token_name, *params.params[i].defaultType);
            }
        }
        return ctx;
    }
};

#endif //DJINN_GENERIC_H