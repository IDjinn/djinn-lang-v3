//
// Interface declaration collection
//

#include "../Binder.h"

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