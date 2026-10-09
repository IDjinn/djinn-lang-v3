//
// Function and extern function declaration collection
//

#include "../Binder.h"

void Binder::collectExternFunction(const ExternFunctionDeclaration& decl, const std::string& prefix) const
{
    const auto funcSym = std::make_shared<ExternFunctionSymbol>(decl.name.token_name, *decl.returnType);
    funcSym->isVariadic = decl.isVariadic;
    funcSym->abi = decl.abi;
    funcSym->isFromLibrary = _bindingStdLib;

    for (const auto& contract : decl.contracts)
    {
        funcSym->contracts.push_back(&contract);
    }

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
    funcSym->isFromLibrary = _bindingStdLib;
    funcSym->isAsync = decl.isAsync;
    funcSym->constEval = decl.constEval;
    funcSym->constExpr = decl.constExpr;
    funcSym->throwsAny = decl.throwsAny;
    funcSym->throwsTypes = decl.throwsTypes;
    for (const auto& contract : decl.contracts)
    {
        funcSym->contracts.push_back(&contract);
    }
    for (const auto& section : decl.sections)
    {
        funcSym->sections.push_back(&section);
    }
    funcSym->uowName = decl.uowName;
    funcSym->uowLocation = decl.uowLocation;
    funcSym->uowPhase = decl.uowPhase;
    // Contracts implicitly throw ContractViolation on violation
    if (!decl.contracts.empty() && !funcSym->throwsAny)
    {
        const auto contractViolation = Type::struct_type("ContractViolation");
        if (std::ranges::find_if(funcSym->throwsTypes, [](const Type& t)
        {
            return t.kind == TypeKind::STRUCT && t.structName == "ContractViolation";
        }) == funcSym->throwsTypes.end())
        {
            funcSym->throwsTypes.push_back(contractViolation);
        }
    }

    // Function-level handler suffix
    for (const auto& arm : decl.catchArms)
    {
        funcSym->catchArms.push_back(&arm);
        const auto& armName = arm.errorType.token_name;
        if (armName == "Error" || armName == "_") funcSym->catchesAllErrors = true;
    }
    funcSym->finallyBlock = decl.finallyBlock.get();
    if (!funcSym->catchArms.empty() || funcSym->finallyBlock)
    {
        if (funcSym->isAsync)
        {
            BINDER_ERROR(DiagnosticCode::INVALID_MODIFIERS,
                         "function '" + decl.name.token_name + "' cannot combine 'async' with a "
                             "catch suffix (deferred error travel across await is a later phase)",
                         decl, decl.name.location);
        }
        if (!nativeExceptions_)
        {
            BINDER_ERROR(DiagnosticCode::TRY_CATCH_REQUIRES_EXCEPTIONS,
                         "a catch suffix requires the exceptions mode ('--exceptions' or "
                             "compiler.exceptions in djinn.proj)",
                         decl, decl.name.location);
        }
    }
    funcSym->throwsAfterArms = throws_after_arms(funcSym->catchArms, funcSym->catchesAllErrors,
                                                 funcSym->throwsAny, funcSym->throwsTypes);

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
        // Uow members take struct parameters by reference (spec §5.1): the
        // transaction's commit/rollback surface is only meaningful when the
        // member mutates caller state, so struct params lower to pointers and
        // auto-deref at every use site like `this` does.
        Type paramType = !decl.uowName.empty() && param.type->kind == TypeKind::STRUCT
                             ? Type::pointer(*param.type)
                             : *param.type;
        funcSym->addParameter(param.name.token_name, paramType, false, std::move(paramAttrs));
    }

    if (!_global_scope->defineFunction(funcSym))
    {
        BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION, "function '" + qualifiedName + "' is already defined", decl,
                     decl.name.location);
    }

    funcSym->setBody(std::move(decl.body));
}