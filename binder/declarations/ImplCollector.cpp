//
// Impl declaration collection - adds methods from impl blocks to existing struct symbols
// or creates synthetic struct symbols for primitive types
//

#include "../Binder.h"
#include <unordered_set>

static const std::unordered_set<std::string> knownAttributes = {
    "intrinsic", "no_mangle", "attribute", "sync",
    "ForceInline", "NoInline", "NoReturn", "Hot", "Cold",
    "NoSync", "NoUnwind", "WillReturn", "NoRecurse",
    "Align", "Volatile", "Restrict", "NoCapture",
    "ReadOnly", "WriteOnly", "NonNull", "NoMangle",
    "Deprecated", "Llvm", "Location"
};

void Binder::collectImpl(const ImplDeclaration& decl, const std::string& prefix) const
{
    for (size_t targetTypeIndex = 0; targetTypeIndex < decl.targetTypes.size(); ++targetTypeIndex)
    {
        const auto targetType = decl.targetTypes.at(targetTypeIndex);
        const std::string typeName = decl.targetTypeName(targetTypeIndex);
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
            structSym->baseType = std::make_unique<Type>(targetType);

            if (!_global_scope->defineStruct(structSym))
            {
                BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION,
                             "type '" + typeName + "' is already defined", decl, targetType.location);
                return;
            }
        }

        // Add fields from impl block to the struct symbol
        for (const auto& field : decl.fields)
        {
            structSym->addField(
                field.name.token_name,
                *field.type,
                false, // isMutable
                field.isConstant,
                field.initializer.get(),
                field.name.location
            );
        }

        // Add methods from impl block to the struct symbol
        for (const auto& method : decl.methods)
        {
            Type methodReturnType = *method->returnType;

            const auto methodSym = std::make_shared<MethodSymbol>(method->name.token_name, methodReturnType);
            methodSym->isAbstract = method->isAbstract();
            methodSym->isStatic = method->isStatic() || method->isOperatorMethod;
            if (method->variadic)
                methodSym->variadicName = method->variadic->token_name;
            methodSym->isAsync = method->isAsync;
            methodSym->isOperator = method->isOperatorMethod;
            methodSym->operatorCanonicalName = method->operatorCanonicalName;

            // Copy and validate attributes from AST
            for (const auto& attr : method->attributes)
            {
                if (!knownAttributes.contains(attr.name.token_name))
                {
                    BINDER_ERROR(DiagnosticCode::UNEXPECTED_TOKEN,
                                 "unknown attribute '" + attr.name.token_name + "'",
                                 attr.name, attr.location);
                }
                validateAttributeTarget(attr.name.token_name, TargetMethod, attr.location);
                methodSym->attributes.emplace_back(attr.name.token_name, attr.args);
            }

            // Store pointers to AST body (AST owns the memory)
            methodSym->body = method->body.get();
            methodSym->expressionBody = method->expression.get();

            for (const auto& param : method->parameters)
            {
                std::vector<AttributeSymbol> paramAttrs;
                for (const auto& attr : param.attributes)
                    paramAttrs.emplace_back(attr.name.token_name, attr.args);
                methodSym->addParameter(param.name.token_name, *param.type, std::move(paramAttrs));
            }

            // Add variadic arr<object> parameter AFTER normal params
            if (method->variadic)
            {
                Type objectType = Type::struct_type("object");
                Type arrObjectType = Type::array(objectType);
                methodSym->addParameter(method->variadic->token_name, arrObjectType);
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
            for (const auto& ifaceName : decl.interfaceNames)
            {
                structSym->addImplements(ifaceName);

                const auto& interfaceSymbol = _current_scope->lookupInterface(ifaceName);
                if (!interfaceSymbol)
                {
                    BINDER_ERROR(DiagnosticCode::UNDEFINED_INTERFACE,
                                 "interface '" + ifaceName + "' has not been defined",
                                 decl, targetType.location);
                    continue;
                }

                // Collect all generic param names from interface + method level
                std::set<std::string> ifaceGenericNames;
                for (const auto& gp : interfaceSymbol->genericParams)
                    ifaceGenericNames.insert(gp.name);

                for (const auto& ifaceMethod : interfaceSymbol->methods)
                {
                    const auto it = std::ranges::find_if(structSym->methods,
                                                         [&](const auto& m)
                                                         {
                                                             // Match by canonical name for operators, by name for regular methods
                                                             if (ifaceMethod->isOperator && m->isOperator)
                                                                 return m->operatorCanonicalName == ifaceMethod->
                                                                     operatorCanonicalName;
                                                             return m->name == ifaceMethod->name;
                                                         });

                    if (it == structSym->methods.end())
                    {
                        BINDER_ERROR(DiagnosticCode::MISSING_INTERFACE_METHOD_IMPLEMENTATION,
                                     "method '" + ifaceName + "." + ifaceMethod->name +
                                     "()' has not been implemented for type '" + typeName + "'",
                                     decl, targetType.location);
                        continue;
                    }

                    const auto& implMethod = *it;

                    // Also include method-level generic params
                    std::set<std::string> allGenericNames = ifaceGenericNames;
                    for (const auto& gp : ifaceMethod->genericParams)
                        allGenericNames.insert(gp.name);

                    // Validate return type (skip if it's a generic parameter)
                    const auto& expectedRet = ifaceMethod->returnType;
                    const bool returnIsGeneric = expectedRet.kind == TypeKind::STRUCT
                        && allGenericNames.contains(expectedRet.structName);

                    if (!returnIsGeneric && expectedRet.toHumanString() != implMethod->returnType.toHumanString())
                    {
                        BINDER_ERROR(DiagnosticCode::IMPL_METHOD_SIGNATURE_MISMATCH,
                                     "method '" + ifaceName + "." + ifaceMethod->name +
                                     "()' expects return type '" + expectedRet.toHumanString() +
                                     "' but implementation for '" + typeName +
                                     "' returns '" + implMethod->returnType.toHumanString() + "'",
                                     decl, targetType.location);
                    }

                    // Validate parameter count
                    if (ifaceMethod->paramTypes.size() != implMethod->paramTypes.size())
                    {
                        BINDER_ERROR(DiagnosticCode::IMPL_METHOD_SIGNATURE_MISMATCH,
                                     "method '" + ifaceName + "." + ifaceMethod->name +
                                     "()' expects " + std::to_string(ifaceMethod->paramTypes.size()) +
                                     " parameter(s) but implementation for '" + typeName +
                                     "' has " + std::to_string(implMethod->paramTypes.size()),
                                     decl, targetType.location);
                    }
                    else
                    {
                        // Validate parameter types
                        // Generic params (T) in the interface are substituted with the target type
                        const std::string targetTypeStr = targetType.toHumanString();

                        for (size_t p = 0; p < ifaceMethod->paramTypes.size(); ++p)
                        {
                            const auto& expectedParam = ifaceMethod->paramTypes[p];
                            const auto& actualParam = implMethod->paramTypes[p];

                            // If interface param is a generic name, expected type is the target type
                            const bool paramIsGeneric = expectedParam.kind == TypeKind::STRUCT
                                && allGenericNames.contains(expectedParam.structName);

                            const std::string expectedStr = paramIsGeneric
                                                                ? targetTypeStr
                                                                : expectedParam.toHumanString();

                            if (expectedStr != actualParam.toHumanString())
                            {
                                BINDER_ERROR(DiagnosticCode::IMPL_METHOD_SIGNATURE_MISMATCH,
                                             "method '" + ifaceName + "." + ifaceMethod->name +
                                             "()' parameter '" + ifaceMethod->paramNames[p] +
                                             "' expects type '" + expectedStr +
                                             "' but implementation for '" + typeName +
                                             "' has '" + actualParam.toHumanString() + "'",
                                             decl, targetType.location);
                            }
                        }
                    }
                }
            }
        }
    }
}