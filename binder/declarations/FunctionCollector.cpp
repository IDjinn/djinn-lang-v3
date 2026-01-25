//
// Function and extern function declaration collection
//

#include "../Binder.h"

void Binder::collectExternFunction(const ExternFunctionDeclaration &decl) const {
    const auto funcSym = std::make_shared<ExternFunctionSymbol>(decl.name.token_name, *decl.returnType);
    funcSym->isVariadic = decl.isVariadic;
    funcSym->abi = decl.abi;

    for (const auto &param: decl.parameters) {
        funcSym->addParameter(param.name.token_name, *param.type);
    }

    if (!_global_scope->defineExternFunction(funcSym)) {
        BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION,
                     "extern function '" + decl.name.token_name + "' is already defined", decl, decl.name.location);
    }
}

void Binder::collectFunction(FunctionDeclaration &decl) const {
    collectFunctionWithPrefix(decl, "");
}

void Binder::collectFunctionWithPrefix(FunctionDeclaration &decl, const std::string &prefix) const {
    // "main" is always in global namespace
    const std::string qualifiedName = (decl.name.token_name == "main" || prefix.empty())
                                          ? decl.name.token_name
                                          : prefix + "::" + decl.name.token_name;
    const auto funcSym = std::make_shared<FunctionSymbol>(qualifiedName, *decl.returnType);

    for (const auto &param: decl.parameters) {
        funcSym->addParameter(param.name.token_name, *param.type);
    }

    if (!_global_scope->defineFunction(funcSym)) {
        BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION, "function '" + qualifiedName + "' is already defined", decl,
                     decl.name.location);
    }

    funcSym->setBody(std::move(decl.body));
}