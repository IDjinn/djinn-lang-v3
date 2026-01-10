//
// Created by Claude on 04/01/2026.
//

#ifndef DJINN_SYMBOL_TABLE_H
#define DJINN_SYMBOL_TABLE_H

#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <optional>
#include <ranges>

#include "Symbol.h"

class SymbolTable {
public:
    explicit SymbolTable(std::shared_ptr<SymbolTable> parent = nullptr)
        : _parent(std::move(parent)) {
        if (_parent) {
            _depth = _parent->_depth + 1;
        }
    }

    [[nodiscard]] std::shared_ptr<SymbolTable> parent() const { return _parent; }
    [[nodiscard]] int depth() const { return _depth; }
    [[nodiscard]] bool isGlobalScope() const { return _parent == nullptr; }

    bool define(std::shared_ptr<Symbol> symbol) {
        const auto &name = symbol->name;
        if (_symbols.contains(name))
            return false;

        _symbols[name] = std::move(symbol);
        return true;
    }

    bool defineAlias(const std::string &alias, std::shared_ptr<Symbol> symbol) {
        if (_symbols.contains(alias))
            return false;

        _symbols[alias] = std::move(symbol);
        return true;
    }

    bool defineVariable(const std::string &name, const Type &type, const bool isMutable, SourceLocation loc = {}) {
        return define(std::make_shared<Symbol>(SymbolKind::Variable, name, type, loc, isMutable));
    }

    bool defineParameter(const std::string &name, const Type &type, const bool isMutable, SourceLocation loc = {}) {
        return define(std::make_shared<Symbol>(SymbolKind::Parameter, name, type, loc, isMutable));
    }

    bool defineFunction(std::shared_ptr<FunctionSymbol> func) {
        return define(std::move(func));
    }

    bool defineStruct(std::shared_ptr<StructSymbol> structSym) {
        return define(std::move(structSym));
    }

    bool defineInterface(std::shared_ptr<InterfaceSymbol> ifaceSym) {
        const std::string key = "interface:" + ifaceSym->name;
        if (_symbols.contains(key))
            return false;
        _symbols[key] = std::move(ifaceSym);
        return true;
    }

    bool defineEnum(std::shared_ptr<EnumSymbol> enumSym) {
        return define(std::move(enumSym));
    }

    [[nodiscard]] std::shared_ptr<Symbol> lookup(const std::string &name) const {
        if (const auto it = _symbols.find(name); it != _symbols.end()) {
            return it->second;
        }

        if (_parent) {
            return _parent->lookup(name);
        }

        return nullptr;
    }

    [[nodiscard]] std::shared_ptr<Symbol> lookupLocal(const std::string &name) const {
        if (const auto it = _symbols.find(name); it != _symbols.end()) {
            return it->second;
        }
        return nullptr;
    }

    [[nodiscard]] std::shared_ptr<Symbol> lookupVariable(const std::string &name) const {
        if (auto symbol = lookup(name); symbol && (symbol->isVariable() || symbol->isParameter())) {
            return symbol;
        }
        return nullptr;
    }

    [[nodiscard]] std::shared_ptr<FunctionSymbol> lookupFunction(const std::string &name) const {
        if (const auto symbol = lookup(name); symbol && symbol->isFunction()) {
            return std::dynamic_pointer_cast<FunctionSymbol>(symbol);
        }
        return nullptr;
    }

    [[nodiscard]] std::shared_ptr<StructSymbol> lookupStruct(const std::string &name) const {
        if (const auto symbol = lookup(name); symbol && symbol->isStruct()) {
            return std::dynamic_pointer_cast<StructSymbol>(symbol);
        }
        return nullptr;
    }

    [[nodiscard]] std::shared_ptr<InterfaceSymbol> lookupInterface(const std::string &name) const {
        const std::string key = "interface:" + name;
        if (const auto it = _symbols.find(key); it != _symbols.end()) {
            if (it->second->isInterface()) {
                return std::dynamic_pointer_cast<InterfaceSymbol>(it->second);
            }
        }
        if (_parent) {
            if (const auto parentTable = std::dynamic_pointer_cast<SymbolTable>(_parent)) {
                return parentTable->lookupInterface(name);
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool hasInterface(const std::string &name) const {
        return lookupInterface(name) != nullptr;
    }

    [[nodiscard]] std::shared_ptr<EnumSymbol> lookupEnum(const std::string &name) const {
        if (const auto symbol = lookup(name); symbol && symbol->isEnum()) {
            return std::dynamic_pointer_cast<EnumSymbol>(symbol);
        }
        return nullptr;
    }

    [[nodiscard]] bool hasEnum(const std::string &name) const {
        return lookupEnum(name) != nullptr;
    }

    [[nodiscard]] bool isDefined(const std::string &name) const {
        return lookup(name) != nullptr;
    }

    [[nodiscard]] bool isDefinedLocally(const std::string &name) const {
        return _symbols.contains(name);
    }

    [[nodiscard]] const std::unordered_map<std::string, std::shared_ptr<Symbol> > &symbols() const {
        return _symbols;
    }

    void markUsed(const std::string &name) const {
        if (const auto symbol = lookup(name)) {
            symbol->isUsed = true;
        }
    }

    [[nodiscard]] std::vector<std::shared_ptr<Symbol> > getUnusedSymbols() const {
        std::vector<std::shared_ptr<Symbol> > unused;
        for (const auto &symbol: _symbols | std::views::values) {
            if (!symbol->isUsed && (symbol->isVariable() || symbol->isParameter())) {
                unused.push_back(symbol);
            }
        }
        return unused;
    }

private:
    std::shared_ptr<SymbolTable> _parent;
    std::unordered_map<std::string, std::shared_ptr<Symbol> > _symbols;
    int _depth = 0;
};

class ScopedSymbolTable : public SymbolTable, public std::enable_shared_from_this<ScopedSymbolTable> {
public:
    explicit ScopedSymbolTable(std::shared_ptr<ScopedSymbolTable> parent = nullptr)
        : SymbolTable(parent), parentScoped_(std::move(parent)) {
    }

    [[nodiscard]] std::shared_ptr<ScopedSymbolTable> createChildScope() {
        return std::make_shared<ScopedSymbolTable>(shared_from_this());
    }

    [[nodiscard]] std::shared_ptr<ScopedSymbolTable> parentScope() const { return parentScoped_; }

private:
    std::shared_ptr<ScopedSymbolTable> parentScoped_;
};

#endif //DJINN_SYMBOL_TABLE_H