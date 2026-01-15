//
// Struct declaration collection
//

#include "../Binder.h"

void Binder::collectStruct(const StructDeclaration &decl) const {
    const auto structSym = std::make_shared<StructSymbol>(decl.name.token_name);

    for (const auto &genParam: decl.genericParams.params) {
        structSym->addGenericParam(genParam.name.token_name);
    }

    for (const auto &field: decl.fields) {
        if (structSym->hasField(field.name.token_name)) {
            BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION,
                         "field '" + field.name.token_name + "' is already defined", field, field.name.location);
        } else {
            structSym->addField(field.name.token_name, *field.type);
        }
    }

    for (const auto &prop: decl.properties) {
        if (!prop.isAutoProperty() && structSym->hasMember(prop.name.token_name)) {
            BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION,
                         "field '" + prop.name.token_name + "' is already defined", prop, prop.name.location);
        } else {
            structSym->addProperty(prop.name.token_name, *prop.type, prop.hasGetter, prop.hasSetter);
        }
    }

    if (decl.baseType) {
        structSym->baseType = std::make_unique<Type>(*decl.baseType);
    }

    if (!_global_scope->defineStruct(structSym)) {
        BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION, "struct '" + decl.name.token_name + "' is already defined",
                     decl, decl.name.location);
    }
}

void Binder::collectStructWithPrefix(const StructDeclaration &decl, const std::string &prefix) const {
    const std::string qualifiedName = prefix.empty() ? decl.name.token_name : prefix + "::" + decl.name.token_name;
    const auto structSym = std::make_shared<StructSymbol>(qualifiedName);

    for (const auto &genParam: decl.genericParams.params) {
        structSym->addGenericParam(genParam.name.token_name);
    }

    for (const auto &field: decl.fields) {
        if (structSym->hasField(field.name.token_name)) {
            BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION,
                         "field '" + field.name.token_name + "' is already defined", field, field.name.location);
        } else {
            structSym->addField(field.name.token_name, *field.type);
        }
    }

    for (const auto &prop: decl.properties) {
        if (!prop.isAutoProperty() && structSym->hasMember(prop.name.token_name)) {
            BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION,
                         "field '" + prop.name.token_name + "' is already defined", prop, prop.name.location);
        } else {
            structSym->addProperty(prop.name.token_name, *prop.type, prop.hasGetter, prop.hasSetter);
        }
    }

    for (const auto &method: decl.methods) {
        const auto methodSym = std::make_shared<MethodSymbol>(method->name.token_name, *method->returnType);
        methodSym->isAbstract = method->isAbstract();

        for (const auto &param: method->parameters) {
            methodSym->addParameter(param.name.token_name, *param.type);
        }

        for (const auto &genParam: method->genericParams.params) {
            methodSym->addGenericParam(genParam.name.token_name);
        }

        structSym->addMethod(methodSym);
    }

    for (const auto &ifaceName: decl.implements) {
        structSym->addImplements(ifaceName);
    }

    if (decl.baseType) {
        structSym->baseType = std::make_unique<Type>(*decl.baseType);
    }

    if (!_global_scope->defineStruct(structSym)) {
        BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION, "struct '" + qualifiedName + "' is already defined", decl,
                     decl.name.location);
    }
}