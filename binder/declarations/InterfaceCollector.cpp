//
// Interface declaration collection
//

#include "../Binder.h"

void Binder::collectInterface(const InterfaceDeclaration &decl) const {
    collectInterfaceWithPrefix(decl, "");
}

void Binder::collectInterfaceWithPrefix(const InterfaceDeclaration &decl, const std::string &prefix) const {
    const std::string qualifiedName = prefix.empty() ? decl.name.token_name : prefix + "::" + decl.name.token_name;

    // Skip if already collected (e.g., from the interface pre-collection pass)
    if (_global_scope->lookupInterface(qualifiedName)) return;

    const auto ifaceSym = std::make_shared<InterfaceSymbol>(qualifiedName);

    for (const auto &genParam: decl.genericParams.params) {
        ifaceSym->addGenericParam(genParam.name.token_name);
    }

    for (const auto &method: decl.methods) {
        const auto methodSym = std::make_shared<MethodSymbol>(method->name.token_name, *method->returnType);
        methodSym->isAbstract = true;

        for (const auto &param: method->parameters) {
            methodSym->addParameter(param.name.token_name, *param.type);
        }

        for (const auto &genParam: method->genericParams.params) {
            methodSym->addGenericParam(genParam.name.token_name);
        }

        ifaceSym->addMethod(methodSym);
    }

    if (!_global_scope->defineInterface(ifaceSym)) {
        BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION, "interface '" + qualifiedName + "' is already defined", decl,
                     decl.name.location);
    }
}