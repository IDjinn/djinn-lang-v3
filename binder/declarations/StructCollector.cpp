//
// Struct declaration collection
//

#include "../Binder.h"
#include <unordered_set>

static const std::unordered_set<std::string> knownAttributes = {
    "intrinsic", "no_mangle", "attribute", "sync",
    "ForceInline", "NoInline", "NoReturn", "Hot", "Cold",
    "NoSync", "NoUnwind", "WillReturn", "NoRecurse",
    "Align", "Volatile", "Restrict", "NoCapture",
    "ReadOnly", "WriteOnly", "NonNull", "NoMangle",
    "Deprecated", "Llvm", "Location", "Reflect"
};

void Binder::collectStruct(const StructDeclaration& decl, const std::string& prefix) const
{
    const std::string qualifiedName = prefix.empty() ? decl.name.token_name : prefix + "::" + decl.name.token_name;
    const auto structSym = std::make_shared<StructSymbol>(qualifiedName);

    // Generic parameters
    for (const auto& genParam : decl.genericParams.params)
    {
        structSym->addGenericParam(genParam.name.token_name, genParam.constraints);
    }

    // Validate constraint interface names
    for (const auto& genParam : decl.genericParams.params)
    {
        for (const auto& constraintName : genParam.constraints)
        {
            if (!_current_scope->lookupInterface(constraintName))
            {
                BINDER_ERROR(DiagnosticCode::UNDEFINED_INTERFACE,
                             "constraint interface '" + constraintName + "' has not been defined",
                             decl, genParam.name.location);
            }
        }
    }

    // Fields
    for (const auto& field : decl.fields)
    {
        if (structSym->hasField(field.name.token_name))
        {
            BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION,
                         "field '" + field.name.token_name + "' is already defined", field, field.name.location);
        }
        else
        {
            bool isConst = field.isConstant || decl.isConstExpr;
            structSym->addField(field.name.token_name, *field.type, false,
                                isConst, field.initializer.get(), field.name.location);
        }
    }

    // Properties
    for (const auto& prop : decl.properties)
    {
        if (!prop.isAutoProperty() && structSym->hasMember(prop.name.token_name))
        {
            BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION,
                         "field '" + prop.name.token_name + "' is already defined", prop, prop.name.location);
        }
        else
        {
            structSym->addProperty(prop.name.token_name, *prop.type, prop.hasGetter, prop.hasSetter);
        }
    }

    // Methods
    for (const auto& method : decl.methods)
    {
        const bool isConstructorMethod = method->isConstructor() ||
            (method->name.token_name == decl.name.token_name);

        const auto methodReturnType = isConstructorMethod
                                          ? Type::struct_type(qualifiedName)
                                          : *method->returnType;

        const auto methodSym = std::make_shared<MethodSymbol>(method->name.token_name, methodReturnType);
        methodSym->isAbstract = method->isAbstract();
        methodSym->isStatic = method->isStatic() || method->isOperatorMethod;
        if (method->variadic)
            methodSym->variadicName = method->variadic->token_name;
        methodSym->isAsync = method->isAsync;
        methodSym->isOperator = method->isOperatorMethod;
        methodSym->operatorCanonicalName = method->operatorCanonicalName;
        methodSym->throwsAny = method->throwsAny;
        methodSym->throwsTypes = method->throwsTypes;
        methodSym->isConstructor = isConstructorMethod;
        if (isConstructorMethod)
        {
            methodSym->structName = qualifiedName;
        }

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

    // Implements
    for (const auto& ifaceName : decl.implements)
    {
        structSym->addImplements(ifaceName);
    }

    // Attributes
    for (const auto& attr : decl.attributes)
    {
        if (!knownAttributes.contains(attr.name.token_name))
        {
            BINDER_ERROR(DiagnosticCode::UNEXPECTED_TOKEN,
                         "unknown attribute '" + attr.name.token_name + "'",
                         attr.name, attr.location);
        }
        validateAttributeTarget(attr.name.token_name, TargetStruct, attr.location);
        structSym->attributes.emplace_back(attr.name.token_name, attr.args);
    }

    // Base type
    if (decl.baseType)
    {
        structSym->baseType = std::make_unique<Type>(*decl.baseType);
    }

    for (const auto& interface_name : structSym->implements)
    {
        const auto& interface_symbol = _current_scope->lookupInterface(interface_name);
        if (!interface_symbol)
        {
            BINDER_ERROR(DiagnosticCode::UNDEFINED_INTERFACE, "interface '" + interface_name + "' has not been defined",
                         decl, decl.name.location);
            continue;
        }


        for (const auto& method_symbol : interface_symbol->methods)
        {
            if (std::ranges::find_if(structSym->methods,
                                     [&](const auto& m) { return m->name == method_symbol->name; })
                != structSym->methods.end())
                continue;

            BINDER_ERROR(DiagnosticCode::MISSING_INTERFACE_METHOD_IMPLEMENTATION,
                         "method '" + interface_name + '.' + method_symbol->name +
                         "()' has not been implemented for struct '" + structSym->name + "'",
                         decl, decl.name.location);
        }
    }

    // Define in global scope
    if (!_global_scope->defineStruct(structSym))
    {
        BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION, "struct '" + qualifiedName + "' is already defined", decl,
                     decl.name.location);
    }
}