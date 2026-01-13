//
// Function and extern function declaration collection
//

#include "../Binder.h"

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