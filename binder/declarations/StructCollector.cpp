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
            errorDuplicateDefinition(field.name.token_name, SymbolKind::Field, {});
        } else {
            structSym->addField(field.name.token_name, *field.type);
        }
    }

    // Collect properties (access control metadata)
    // Auto-properties have a field with the same name, so skip duplicate check for them
    for (const auto &prop: decl.properties) {
        if (!prop.isAutoProperty() && structSym->hasMember(prop.name.token_name)) {
            errorDuplicateDefinition(prop.name.token_name, SymbolKind::Field, {});
        } else {
            structSym->addProperty(prop.name.token_name, *prop.type, prop.hasGetter, prop.hasSetter);
        }
    }

    // Collect base type for transparent types (struct Size : i32;)
    if (decl.baseType) {
        structSym->baseType = std::make_unique<Type>(*decl.baseType);
    }

    if (!_global_scope->defineStruct(structSym)) {
        errorDuplicateDefinition(decl.name.token_name, SymbolKind::Struct, {});
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
            errorDuplicateDefinition(field.name.token_name, SymbolKind::Field, {});
        } else {
            structSym->addField(field.name.token_name, *field.type);
        }
    }

    // Collect properties (access control metadata)
    // Auto-properties have a field with the same name, so skip duplicate check for them
    for (const auto &prop: decl.properties) {
        if (!prop.isAutoProperty() && structSym->hasMember(prop.name.token_name)) {
            errorDuplicateDefinition(prop.name.token_name, SymbolKind::Field, {});
        } else {
            structSym->addProperty(prop.name.token_name, *prop.type, prop.hasGetter, prop.hasSetter);
        }
    }

    // Collect methods
    for (const auto &method: decl.methods) {
        auto methodSym = std::make_shared<MethodSymbol>(method->name.token_name, *method->returnType);
        methodSym->isAbstract = method->isAbstract();

        for (const auto &param: method->parameters) {
            methodSym->addParameter(param.name.token_name, *param.type);
        }

        for (const auto &genParam: method->genericParams.params) {
            methodSym->addGenericParam(genParam.name.token_name);
        }

        structSym->addMethod(methodSym);
    }

    // Collect implements
    for (const auto &ifaceName: decl.implements) {
        structSym->addImplements(ifaceName);
    }

    // Collect base type for transparent types (struct Size : i32;)
    if (decl.baseType) {
        structSym->baseType = std::make_unique<Type>(*decl.baseType);
    }

    if (!_global_scope->defineStruct(structSym)) {
        errorDuplicateDefinition(qualifiedName, SymbolKind::Struct, {});
    }
}