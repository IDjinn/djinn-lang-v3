//
// Struct declaration collection
//

#include "../Binder.h"

void Binder::collectStruct(const StructDeclaration &decl) const {
    const auto structSym = std::make_shared<StructSymbol>(decl.name);

    for (const auto &genParam: decl.genericParams.params) {
        structSym->addGenericParam(genParam.name);
    }

    for (const auto &field: decl.fields) {
        if (structSym->hasField(field.name)) {
            errorDuplicateDefinition(field.name, SymbolKind::Field, {});
        } else {
            structSym->addField(field.name, *field.type);
        }
    }

    // Collect properties (access control metadata)
    // Auto-properties have a field with the same name, so skip duplicate check for them
    for (const auto &prop: decl.properties) {
        if (!prop.isAutoProperty() && structSym->hasMember(prop.name)) {
            errorDuplicateDefinition(prop.name, SymbolKind::Field, {});
        } else {
            structSym->addProperty(prop.name, *prop.type, prop.hasGetter, prop.hasSetter);
        }
    }

    // Collect base type for transparent types (struct Size : i32;)
    if (decl.baseType) {
        structSym->baseType = std::make_unique<Type>(*decl.baseType);
    }

    if (!_global_scope->defineStruct(structSym)) {
        errorDuplicateDefinition(decl.name, SymbolKind::Struct, {});
    }
}

void Binder::collectStructWithPrefix(const StructDeclaration &decl, const std::string &prefix) const {
    const std::string qualifiedName = prefix.empty() ? decl.name : prefix + "::" + decl.name;
    const auto structSym = std::make_shared<StructSymbol>(qualifiedName);

    for (const auto &genParam: decl.genericParams.params) {
        structSym->addGenericParam(genParam.name);
    }

    for (const auto &field: decl.fields) {
        if (structSym->hasField(field.name)) {
            errorDuplicateDefinition(field.name, SymbolKind::Field, {});
        } else {
            structSym->addField(field.name, *field.type);
        }
    }

    // Collect properties (access control metadata)
    // Auto-properties have a field with the same name, so skip duplicate check for them
    for (const auto &prop: decl.properties) {
        if (!prop.isAutoProperty() && structSym->hasMember(prop.name)) {
            errorDuplicateDefinition(prop.name, SymbolKind::Field, {});
        } else {
            structSym->addProperty(prop.name, *prop.type, prop.hasGetter, prop.hasSetter);
        }
    }

    // Collect methods
    for (const auto &method: decl.methods) {
        auto methodSym = std::make_shared<MethodSymbol>(method->name, *method->returnType);
        methodSym->isAbstract = method->isAbstract();

        for (const auto &param: method->parameters) {
            methodSym->addParameter(param.name, *param.type);
        }

        for (const auto &genParam: method->genericParams.params) {
            methodSym->addGenericParam(genParam.name);
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