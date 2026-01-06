//
// Created by Claude on 05/01/2026.
//

#include "Binder.h"

void Binder::collectDeclarations(const Program &program) {
    const std::string filePrefix = program.fileNamespace;

    for (const auto &ext: program.externFunctions) {
        collectExternFunction(*ext);
    }

    // Collect interfaces first (so structs can reference them)
    for (const auto &iface: program.interfaces) {
        collectInterfaceWithPrefix(*iface, filePrefix);
    }

    for (const auto &struc: program.structs) {
        collectStructWithPrefix(*struc, filePrefix);
    }

    for (const auto &func: program.functions) {
        collectFunctionWithPrefix(*func, filePrefix);
    }

    for (const auto &ns: program.namespaces) {
        collectNamespace(*ns, "");
    }
}

void Binder::collectExternFunction(const ExternFunctionDeclaration &decl) const {
    const auto funcSym = std::make_shared<FunctionSymbol>(decl.name, *decl.returnType);
    funcSym->kind = SymbolKind::ExternFunction;
    funcSym->isVariadic = decl.isVariadic;

    for (const auto &param: decl.parameters) {
        funcSym->addParameter(param.name, *param.type);
    }

    if (!_global_scope->defineFunction(funcSym)) {
        errorDuplicateDefinition(decl.name, SymbolKind::ExternFunction, {});
    }
}

void Binder::collectStruct(const StructDeclaration &decl) const {
    const auto structSym = std::make_shared<StructSymbol>(decl.name);

    for (const auto &genParam: decl.genericParams.params) {
        structSym->addGenericParam(genParam.name);
    }

    for (const auto &field: decl.fields) {
        if (structSym->hasField(field.name)) {
            errorDuplicateDefinition(field.name, SymbolKind::Field, {});
        } else {
            structSym->addField(field.name, *field.type);
        }
    }

    // Collect base type for transparent types (struct Size : i32;)
    if (decl.baseType) {
        structSym->baseType = std::make_unique<Type>(*decl.baseType);
    }

    if (!_global_scope->defineStruct(structSym)) {
        errorDuplicateDefinition(decl.name, SymbolKind::Struct, {});
    }
}

void Binder::collectFunction(const FunctionDeclaration &decl) const {
    collectFunctionWithPrefix(decl, "");
}

void Binder::collectFunctionWithPrefix(const FunctionDeclaration &decl, const std::string &prefix) const {
    // "main" is always in global namespace
    const std::string qualifiedName = (decl.name == "main" || prefix.empty())
                                          ? decl.name
                                          : prefix + "::" + decl.name;
    const auto funcSym = std::make_shared<FunctionSymbol>(qualifiedName, *decl.returnType);

    for (const auto &param: decl.parameters) {
        funcSym->addParameter(param.name, *param.type);
    }

    if (!_global_scope->defineFunction(funcSym)) {
        errorDuplicateDefinition(qualifiedName, SymbolKind::Function, {});
    }
}

void Binder::collectInterface(const InterfaceDeclaration &decl) const {
    collectInterfaceWithPrefix(decl, "");
}

void Binder::collectInterfaceWithPrefix(const InterfaceDeclaration &decl, const std::string &prefix) const {
    const std::string qualifiedName = prefix.empty() ? decl.name : prefix + "::" + decl.name;
    const auto ifaceSym = std::make_shared<InterfaceSymbol>(qualifiedName);

    for (const auto &genParam: decl.genericParams.params) {
        ifaceSym->addGenericParam(genParam.name);
    }

    for (const auto &method: decl.methods) {
        auto methodSym = std::make_shared<MethodSymbol>(method->name, *method->returnType);
        methodSym->isAbstract = true;

        for (const auto &param: method->parameters) {
            methodSym->addParameter(param.name, *param.type);
        }

        for (const auto &genParam: method->genericParams.params) {
            methodSym->addGenericParam(genParam.name);
        }

        ifaceSym->addMethod(methodSym);
    }

    if (!_global_scope->defineInterface(ifaceSym)) {
        errorDuplicateDefinition(qualifiedName, SymbolKind::Interface, {});
    }
}

void Binder::collectStructWithPrefix(const StructDeclaration &decl, const std::string &prefix) const {
    const std::string qualifiedName = prefix.empty() ? decl.name : prefix + "::" + decl.name;
    const auto structSym = std::make_shared<StructSymbol>(qualifiedName);

    for (const auto &genParam: decl.genericParams.params) {
        structSym->addGenericParam(genParam.name);
    }

    for (const auto &field: decl.fields) {
        if (structSym->hasField(field.name)) {
            errorDuplicateDefinition(field.name, SymbolKind::Field, {});
        } else {
            structSym->addField(field.name, *field.type);
        }
    }

    // Collect methods
    for (const auto &method: decl.methods) {
        auto methodSym = std::make_shared<MethodSymbol>(method->name, *method->returnType);
        methodSym->isAbstract = method->isAbstract();

        for (const auto &param: method->parameters) {
            methodSym->addParameter(param.name, *param.type);
        }

        for (const auto &genParam: method->genericParams.params) {
            methodSym->addGenericParam(genParam.name);
        }

        structSym->addMethod(methodSym);
    }

    // Collect implements
    for (const auto &ifaceName: decl.implements) {
        structSym->addImplements(ifaceName);
    }

    // Collect base type for transparent types (struct Size : i32;)
    if (decl.baseType) {
        structSym->baseType = std::make_unique<Type>(*decl.baseType);
    }

    if (!_global_scope->defineStruct(structSym)) {
        errorDuplicateDefinition(qualifiedName, SymbolKind::Struct, {});
    }
}

void Binder::collectNamespace(const NamespaceDeclaration &ns, const std::string &prefix) const {
    const std::string qualifiedPrefix = prefix.empty() ? ns.name : prefix + "::" + ns.name;

    // Collect structs in namespace
    for (const auto &struc: ns.structs) {
        collectStructWithPrefix(*struc, qualifiedPrefix);
    }

    // Collect functions in namespace
    for (const auto &func: ns.functions) {
        collectFunctionWithPrefix(*func, qualifiedPrefix);
    }

    // Recursively collect nested namespaces
    for (const auto &nestedNs: ns.namespaces) {
        collectNamespace(*nestedNs, qualifiedPrefix);
    }
}

void Binder::processImports(const Program &program) const {
    // Create aliases for symbols in the file's own namespace
    if (!program.fileNamespace.empty()) {
        const std::string filePrefix = program.fileNamespace + "::";
        for (const auto &[name, symbol]: _global_scope->symbols()) {
            if (name.starts_with(filePrefix)) {
                const std::string shortName = name.substr(filePrefix.length());
                // Only alias if it's a direct member (no nested ::)
                if (shortName.find("::") == std::string::npos) {
                    if (!_global_scope->isDefinedLocally(shortName)) {
                        _global_scope->defineAlias(shortName, symbol);
                    }
                }
            }
        }
    }

    // Process explicit imports
    for (const auto &import: program.imports) {
        const std::string nsPath = import->namespacePath.toString();

        for (const auto &[name, symbol]: _global_scope->symbols()) {
            if (name.starts_with(nsPath + "::")) {
                if (const std::string shortName = name.substr(nsPath.length() + 2);
                    shortName.find("::") == std::string::npos) {
                    if (!_global_scope->isDefinedLocally(shortName)) {
                        _global_scope->defineAlias(shortName, symbol);
                    }
                }
            }
        }
    }
}