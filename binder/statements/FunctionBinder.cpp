//
// Function, method and namespace binding - uses visitor pattern
//

#include <assert.h>

#include "../Binder.h"
#include "../visitors/ProgramBinderVisitor.h"

void Binder::bindProgram(const Program& program)
{
    djinn::ProgramBinderVisitor visitor(*this);

    for (const auto& ext : program.externFunctions)
    {
        ext->accept(visitor, program.fileNamespace);
    }

    // Bind constexpr initializers so overflow modes and types are recorded on
    // the AST before the generator's ConstEvaluator runs (it relies on the
    // binder-set overflowMode for saturating/trapped arithmetic)
    for (const auto& ce : program.constExprs)
    {
        if (ce->isIntrinsic || !ce->value) continue;
        try
        {
            bindExpression(*ce->value);
        }
        catch (const CompileError&)
        {
            // already reported as a diagnostic; keep binding the rest
        }
    }

    for (const auto& struc : program.structs)
    {
        struc->accept(visitor, program.fileNamespace);
    }

    for (const auto& func : program.functions)
    {
        func->accept(visitor, program.fileNamespace);
    }

    for (const auto& ns : program.namespaces)
    {
        ns->accept(visitor, "");
    }
}

void Binder::bindFunction(const FunctionDeclaration& func, const std::string& prefix)
{
    const std::string qualifiedName = (func.name.token_name == "main" || prefix.empty())
                                          ? func.name.token_name
                                          : prefix + "::" + func.name.token_name;
    currentFunction_ = qualifiedName;

    const auto funcSym = _global_scope->lookupFunction(qualifiedName);
    currentFunctionThrows_ = funcSym ? funcSym->effectivelyThrowing() : false;
    currentFunctionThrowsAny_ = funcSym ? funcSym->throwsAny : false;
    currentFunctionThrowsTypes_ = funcSym ? funcSym->throwsTypes : std::vector<Type>();
    currentFunctionCatchesAll_ = funcSym ? funcSym->catchesAllErrors : false;
    currentFunctionCaughtNames_.clear();
    if (funcSym)
    {
        for (const auto* arm : funcSym->catchArms)
            currentFunctionCaughtNames_.push_back(arm->errorType.token_name);
    }

    pushScope();

    size_t paramIdx = 0;
    for (const auto& param : func.parameters)
    {
        if (!isTypeDefined(*param.type) && param.type->kind == TypeKind::STRUCT)
        {
            BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT, "undefined struct '" + param.type->structName + "'", param,
                         param.name.location);
        }

        // Uow members bind reference parameters as pointers (matching the
        // collected symbol) so field access auto-derefs like `this` does.
        const Type paramType = funcSym && paramIdx < funcSym->paramTypes.size()
                                   ? funcSym->paramTypes[paramIdx]
                                   : *param.type;

        if (!_current_scope->defineParameter(param.name.token_name, paramType, param.isMutable))
        {
            BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION,
                         "parameter '" + param.name.token_name + "' is already defined", param, param.name.location);
        }
        paramIdx++;
    }

    if (!isTypeDefined(*func.returnType))
    {
        if (func.returnType->kind == TypeKind::STRUCT)
        {
            BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT, "undefined struct '" + func.returnType->structName + "'",
                         func, func.name.location);
        }
    }

    // Bind require/ensure contract conditions (params are in scope)
    if (funcSym)
    {
        bind_contract_conditions(funcSym->contracts, *func.returnType);

        // `require(p != 0)` proves the parameter non-zero for the whole body
        // (symbol/AST upgrades already ran in the bindAll pre-pass)
        for (const auto& proven : apply_non_zero_contract_upgrades(*funcSym, func))
        {
            if (auto var = _current_scope->lookupVariable(proven))
            {
                var->type.nonZero = true;
            }
        }
    }

    // Get the body from the FunctionSymbol (ownership was transferred during collection)
    if (funcSym && funcSym->body)
    {
        // A catch suffix turns the whole body into a checked scope: throwing
        // calls are handled by the arms, not by the function's propagation.
        const bool prevInsideTry = insideTryExpression_;
        if (!funcSym->catchArms.empty()) insideTryExpression_ = true;
        bindBlock(*funcSym->body);
        insideTryExpression_ = prevInsideTry;
    }

    // Handler suffix: catch arms (each its own scope with the binding) + finally.
    if (funcSym)
    {
        for (const auto* arm : funcSym->catchArms)
        {
            const auto& armName = arm->errorType.token_name;
            if (armName != "_" && armName != "Error")
            {
                const auto errStruct = _global_scope->lookupStruct(armName);
                if (!errStruct || !errStruct->isErrorType)
                {
                    _diagnostics.emitAndPrint(Diagnostic(
                        Severity::Error, DiagnosticCode::CATCH_ARM_NOT_ERROR_TYPE,
                        "catch pattern must be an error type (deriving from 'Exception'), "
                            "'Error' or '_', got '" + armName + "'",
                        arm->location
                    ));
                }
            }
            pushScope();
            if (arm->binding)
            {
                _current_scope->defineVariable(arm->binding->token_name,
                                               Type::struct_type(arm->errorType.token_name), false);
            }
            bindBlock(*arm->body);
            popScope();
        }
        if (funcSym->finallyBlock)
        {
            pushScope();
            bindBlock(*funcSym->finallyBlock);
            popScope();
        }
    }

    popScope();
    currentFunction_.clear();
    currentFunctionThrows_ = false;
    currentFunctionThrowsAny_ = false;
    currentFunctionThrowsTypes_.clear();
    currentFunctionCaughtNames_.clear();
    currentFunctionCatchesAll_ = false;
}

void Binder::bindMethod(StructMethodDeclaration& method, const StructDeclaration& struc)
{
    currentFunction_ = struc.name.token_name + "::" + method.name.token_name;
    currentStructName_ = struc.name.token_name;
    std::vector<const CatchClause*> suffixArms;
    bool catchesAll = false;
    for (const auto& arm : method.catchArms)
    {
        suffixArms.push_back(&arm);
        const auto& armName = arm.errorType.token_name;
        if (armName == "Error" || armName == "_") catchesAll = true;
    }
    currentFunctionThrows_ = method.throwsAny ||
        throws_after_arms(suffixArms, catchesAll, method.throwsAny, method.throwsTypes);
    currentFunctionThrowsAny_ = method.throwsAny;
    currentFunctionThrowsTypes_ = method.throwsTypes;
    currentFunctionCatchesAll_ = catchesAll;
    currentFunctionCaughtNames_.clear();
    for (const auto& arm : method.catchArms)
        currentFunctionCaughtNames_.push_back(arm.errorType.token_name);

    pushScope();

    // Define 'this' as pointer to struct type (fields accessed via this.fieldName)
    Type thisType;
    thisType.kind = TypeKind::POINTER;
    thisType.elementType = std::make_unique<Type>();
    thisType.elementType->kind = TypeKind::STRUCT;
    thisType.elementType->structName = struc.name.token_name;
    _current_scope->defineVariable("this", thisType, false);

    for (const auto& param : method.parameters)
    {
        if (!isTypeDefined(*param.type) && !is_generic_type(*param.type, struc))
        {
            if (param.type->kind == TypeKind::STRUCT)
            {
                BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT, "undefined struct '" + param.type->structName + "'",
                             param, param.name.location);
            }
        }
        // Uow members bind reference parameters as pointers so field access
        // auto-derefs like `this` does.
        const Type paramType = !method.uowName.empty() && param.type->kind == TypeKind::STRUCT
                                   ? Type::pointer(*param.type)
                                   : *param.type;
        if (!_current_scope->defineParameter(param.name.token_name, paramType, param.isMutable))
        {
            BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION,
                         "parameter '" + param.name.token_name + "' is already defined", param, param.name.location);
        }
    }

    // Define variadic parameter as arr<object> in scope
    if (method.variadic)
    {
        Type objectType = Type::struct_type("object");
        Type arrObjectType = Type::array(objectType);
        _current_scope->defineParameter(method.variadic->token_name, arrObjectType, false);
    }

    auto returnType = resolveType(*method.returnType);
    if (!returnType)
    {
        if (!is_generic_type(*method.returnType, struc))
        {
            if (method.returnType->kind == TypeKind::STRUCT)
            {
                BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT,
                             "undefined struct '" + method.returnType->structName + "'",
                             method, method.name.location);
            }
        }
        // Keep original method.returnType for generic types or error recovery
    }
    else
    {
        method.returnType = std::move(returnType);
    }
    // Bind require/ensure contract conditions (this and params are in scope)
    {
        std::vector<const ContractClause*> methodContracts;
        for (const auto& contract : method.contracts)
            methodContracts.push_back(&contract);
        bind_contract_conditions(methodContracts, *method.returnType);

        // `require(p != 0)` proves the parameter non-zero for the whole body
        for (const auto* contract : methodContracts)
        {
            if (!contract || !contract->isRequire() || !contract->condition) continue;
            const auto proven = non_zero_proven_identifier(*contract->condition);
            if (!proven) continue;

            bool isParam = false;
            for (auto& param : method.parameters)
            {
                if (param.name.token_name != *proven) continue;
                if (param.type->kind == TypeKind::INTEGER)
                {
                    param.type->nonZero = true;
                    isParam = true;
                }
                break;
            }
            if (!isParam) continue;

            if (auto var = _current_scope->lookupVariable(*proven))
            {
                var->type.nonZero = true;
            }
        }

        // `ensure(return != 0)` proves the return value non-zero for callers
        if (method.returnType->kind == TypeKind::INTEGER && !method.returnType->nonZero)
        {
            for (const auto* contract : methodContracts)
            {
                if (!contract || !contract->isEnsure() || !contract->condition) continue;
                if (!ensures_non_zero_return(*contract->condition)) continue;
                method.returnType->nonZero = true;
                break;
            }
        }
    }

    if (method.body)
    {
        // A catch suffix turns the whole body into a checked scope.
        const bool prevInsideTry = insideTryExpression_;
        if (!method.catchArms.empty()) insideTryExpression_ = true;
        bindBlock(*method.body);
        insideTryExpression_ = prevInsideTry;
    }
    else if (method.expression)
    {
        bindExpression(*method.expression);
    }

    // Handler suffix: catch arms (each its own scope with the binding) + finally.
    for (const auto& arm : method.catchArms)
    {
        const auto& armName = arm.errorType.token_name;
        if (armName != "_" && armName != "Error")
        {
            const auto errStruct = _global_scope->lookupStruct(armName);
            if (!errStruct || !errStruct->isErrorType)
            {
                _diagnostics.emitAndPrint(Diagnostic(
                    Severity::Error, DiagnosticCode::CATCH_ARM_NOT_ERROR_TYPE,
                    "catch pattern must be an error type (deriving from 'Exception'), "
                        "'Error' or '_', got '" + armName + "'",
                    arm.location
                ));
            }
        }
        pushScope();
        if (arm.binding)
        {
            _current_scope->defineVariable(arm.binding->token_name,
                                           Type::struct_type(arm.errorType.token_name), false);
        }
        bindBlock(*arm.body);
        popScope();
    }
    if (method.finallyBlock)
    {
        pushScope();
        bindBlock(*method.finallyBlock);
        popScope();
    }

    // Look up the method in the SPECIFIC struct, not across all structs in scope.
    // lookupMethod searches all structs by name, which causes collisions when two structs
    // in the same namespace have methods with the same name (e.g., array.get vs map.get).
    const auto structSym = _current_scope->lookupStruct(struc.name.token_name);
    if (structSym)
    {
        for (auto& m : structSym->methods)
        {
            if (m->name == method.name.token_name)
            {
                m->returnType = *method.returnType.get();
                break;
            }
        }
    }

    popScope();
    currentFunction_.clear();
    currentStructName_.clear();
    currentFunctionThrows_ = false;
    currentFunctionThrowsAny_ = false;
    currentFunctionThrowsTypes_.clear();
    currentFunctionCaughtNames_.clear();
    currentFunctionCatchesAll_ = false;
}

void Binder::bind_contract_conditions(const std::vector<const ContractClause*>& contracts,
                                      const Type& returnType)
{
    for (const auto& contract : contracts)
    {
        if (!contract || !contract->condition) continue;

        if (contract->isEnsure())
        {
            // Inside ensure, `return` refers to the function's return value
            pushScope();
            _current_scope->defineVariable("return", returnType, false);
            bindExpression(*contract->condition);
            popScope();
        }
        else
        {
            bindExpression(*contract->condition);
        }
    }
}

// Contract-driven non-zero upgrades, applied to the symbol and AST:
// `require(p != 0)` proves a parameter non-zero for the whole body, and
// `ensure(return != 0)` proves the return value non-zero for callers.
// Returns the names of the parameters proven non-zero (for scope upgrades).
std::vector<std::string> Binder::apply_non_zero_contract_upgrades(FunctionSymbol& funcSym,
                                                                  const FunctionDeclaration& func)
{
    std::vector<std::string> provenParams;

    for (const auto* contract : funcSym.contracts)
    {
        if (!contract || !contract->isRequire() || !contract->condition) continue;
        const auto proven = non_zero_proven_identifier(*contract->condition);
        if (!proven) continue;

        bool isParam = false;
        for (size_t i = 0; i < funcSym.paramNames.size(); i++)
        {
            if (funcSym.paramNames[i] != *proven) continue;
            if (funcSym.paramTypes[i].kind == TypeKind::INTEGER)
            {
                funcSym.paramTypes[i].nonZero = true;
                isParam = true;
            }
            break;
        }
        if (!isParam) continue;

        provenParams.push_back(*proven);
        for (auto& param : const_cast<std::vector<Parameter>&>(func.parameters))
        {
            if (param.name.token_name == *proven && param.type->kind == TypeKind::INTEGER)
            {
                param.type->nonZero = true;
            }
        }
    }

    if (funcSym.returnType.kind == TypeKind::INTEGER && !funcSym.returnType.nonZero)
    {
        for (const auto* contract : funcSym.contracts)
        {
            if (!contract || !contract->isEnsure() || !contract->condition) continue;
            if (!ensures_non_zero_return(*contract->condition)) continue;
            funcSym.returnType.nonZero = true;
            funcSym.type.nonZero = true;
            const_cast<Type&>(*func.returnType).nonZero = true;
            break;
        }
    }

    return provenParams;
}

void Binder::bindNamespace(const NamespaceDeclaration& ns, const std::string& prefix)
{
    const std::string qualifiedPrefix = prefix.empty() ? ns.name.token_name : prefix + "::" + ns.name.token_name;

    djinn::ProgramBinderVisitor visitor(*this);

    for (const auto& struc : ns.structs)
    {
        struc->accept(visitor, qualifiedPrefix);
    }

    for (const auto& func : ns.functions)
    {
        func->accept(visitor, qualifiedPrefix);
    }

    for (const auto& nestedNs : ns.namespaces)
    {
        nestedNs->accept(visitor, qualifiedPrefix);
    }
}

void Binder::validateExternFunctionTypes(const ExternFunctionDeclaration& decl)
{
    if (!isTypeDefined(*decl.returnType))
    {
        if (decl.returnType->kind == TypeKind::STRUCT)
        {
            BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT,
                         "undefined struct '" + decl.returnType->structName + "'",
                         &decl, decl.name.location);
        }
    }
    for (const auto& param : decl.parameters)
    {
        if (!isTypeDefined(*param.type))
        {
            if (param.type->kind == TypeKind::STRUCT)
            {
                BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT,
                             "undefined struct '" + param.type->structName + "'",
                             param, param.name.location);
            }
        }
    }
}

void Binder::validateStructFieldTypes(const StructDeclaration& decl)
{
    for (const auto& field : decl.fields)
    {
        if (!isTypeDefined(*field.type))
        {
            if (field.type->kind == TypeKind::STRUCT)
            {
                if (decl.genericParams.find(field.type->structName) == nullptr)
                {
                    BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT,
                                 "undefined struct '" + field.type->structName + "'",
                                 field, field.name.location);
                }
            }
        }
    }
}