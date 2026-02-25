//
// Impl declaration collection - adds methods from impl blocks to existing struct symbols
// or creates synthetic struct symbols for primitive types
//

#include "../Binder.h"

void Binder::collectImpl(const ImplDeclaration& decl, const std::string& prefix) const
{
    const std::string typeName = decl.targetTypeName();
    const std::string qualifiedName = prefix.empty() ? typeName : prefix + "::" + typeName;

    // Try to find an existing struct for this type
    auto structSym = _global_scope->lookupStruct(qualifiedName);

    if (!structSym)
    {
        // Also try without prefix for primitive types (i32, f64, etc.)
        structSym = _global_scope->lookupStruct(typeName);
    }

    if (!structSym)
    {
        // No struct found — this is a primitive type impl.
        // Create a synthetic StructSymbol with no fields.
        structSym = std::make_shared<StructSymbol>(typeName);
        structSym->baseType = std::make_unique<Type>(*decl.targetType);

        if (!_global_scope->defineStruct(structSym))
        {
            BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION,
                         "type '" + typeName + "' is already defined", decl, decl.targetType->location);
            return;
        }
    }

    // Add methods from impl block to the struct symbol
    for (const auto& method : decl.methods)
    {
        Type methodReturnType = *method->returnType;

        const auto methodSym = std::make_shared<MethodSymbol>(method->name.token_name, methodReturnType);
        methodSym->isAbstract = method->isAbstract();
        methodSym->isStatic = method->isStatic();
        methodSym->isVariadic = method->isVariadic;
        methodSym->variadicForwardTarget = method->variadicForwardTarget;

        // Store pointers to AST body (AST owns the memory)
        methodSym->body = method->body.get();
        methodSym->expressionBody = method->expression.get();

        for (const auto& param : method->parameters)
        {
            methodSym->addParameter(param.name.token_name, *param.type);
        }

        for (const auto& genParam : method->genericParams.params)
        {
            methodSym->addGenericParam(genParam.name.token_name, genParam.constraints);
        }

        structSym->addMethod(methodSym);
    }

    // Handle interface impl: validate interface exists and all methods are implemented
    if (decl.isInterfaceImpl())
    {
        structSym->addImplements(decl.interfaceName);

        const auto& interfaceSymbol = _current_scope->lookupInterface(decl.interfaceName);
        if (!interfaceSymbol)
        {
            BINDER_ERROR(DiagnosticCode::UNDEFINED_INTERFACE,
                         "interface '" + decl.interfaceName + "' has not been defined",
                         decl, decl.targetType->location);
            return;
        }

        for (const auto& ifaceMethod : interfaceSymbol->methods)
        {
            if (std::ranges::find_if(structSym->methods,
                                     [&](const auto& m) { return m->name == ifaceMethod->name; })
                != structSym->methods.end())
                continue;

            BINDER_ERROR(DiagnosticCode::MISSING_INTERFACE_METHOD_IMPLEMENTATION,
                         "method '" + decl.interfaceName + "." + ifaceMethod->name +
                         "()' has not been implemented for type '" + typeName + "'",
                         decl, decl.targetType->location);
        }
    }
}