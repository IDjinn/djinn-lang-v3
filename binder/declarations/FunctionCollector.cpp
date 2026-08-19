//
// Function and extern function declaration collection
//

#include "../Binder.h"

void Binder::collectExternFunction(const ExternFunctionDeclaration& decl, const std::string& prefix) const
{
    const auto funcSym = std::make_shared<ExternFunctionSymbol>(decl.name.token_name, *decl.returnType);
    funcSym->isVariadic = decl.isVariadic;
    funcSym->abi = decl.abi;

    for (const auto& param : decl.parameters)
    {
        funcSym->addParameter(param.name.token_name, *param.type);
    }

    if (!_global_scope->defineExternFunction(funcSym))
    {
        BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION,
                     "extern function '" + decl.name.token_name + "' is already defined", decl, decl.name.location);
    }

    // Register qualified name alias for import resolution
    if (!prefix.empty())
    {
        const std::string qualifiedName = prefix + "::" + decl.name.token_name;
        _global_scope->defineAlias(qualifiedName, funcSym);
    }
}

void Binder::collectFunction(FunctionDeclaration& decl) const
{
    collectFunctionWithPrefix(decl, "");
}

void Binder::collectFunctionWithPrefix(FunctionDeclaration& decl, const std::string& prefix) const
{
    // "main" is always in global namespace
    const std::string qualifiedName = (decl.name.token_name == "main" || prefix.empty())
                                          ? decl.name.token_name
                                          : prefix + "::" + decl.name.token_name;
    const auto funcSym = std::make_shared<FunctionSymbol>(qualifiedName, *decl.returnType);
    funcSym->isAsync = decl.isAsync;
    funcSym->constEval = decl.constEval;
    funcSym->constExpr = decl.constExpr;
    funcSym->throwsAny = decl.throwsAny;
    funcSym->throwsTypes = decl.throwsTypes;

    // function cannot be async and compile time constraint
    if (funcSym->isAsync && (funcSym->constEval || funcSym->constExpr))
    {
        const auto invalidModifierStr = funcSym->constEval ? "consteval" : "constexpr";
        BINDER_ERROR(DiagnosticCode::INVALID_MODIFIERS,
                     "function '" + decl.name.token_name +"' cannot have 'async' and '"+invalidModifierStr+"'!", decl,
                     decl.name.location);
    }

    for (const auto& param : decl.parameters)
    {
        std::vector<AttributeSymbol> paramAttrs;
        for (const auto& attr : param.attributes)
            paramAttrs.emplace_back(attr.name.token_name, attr.args);
        funcSym->addParameter(param.name.token_name, *param.type, false, std::move(paramAttrs));
    }

    if (!_global_scope->defineFunction(funcSym))
    {
        BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION, "function '" + qualifiedName + "' is already defined", decl,
                     decl.name.location);
    }

    funcSym->setBody(std::move(decl.body));
}