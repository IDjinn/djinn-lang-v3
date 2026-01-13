//
// Enum declaration collection
//

#include "../Binder.h"

void Binder::collectEnum(const EnumDeclaration &decl) const {
    collectEnumWithPrefix(decl, "");
}

void Binder::collectEnumWithPrefix(const EnumDeclaration &decl, const std::string &prefix) const {
    const std::string qualifiedName = prefix.empty() ? decl.name : prefix + "::" + decl.name;
    const auto enumSym = std::make_shared<EnumSymbol>(qualifiedName);

    // Collect generic parameters
    for (const auto &genParam: decl.genericParams.params) {
        enumSym->addGenericParam(genParam.name);
    }

    for (const auto &variant: decl.values) {
        if (enumSym->hasVariant(variant.name)) {
            errorDuplicateDefinition(variant.name, SymbolKind::Enum, {});
        } else {
            enumSym->addVariant(variant.name, variant.types);
        }
    }

    if (!_global_scope->defineEnum(enumSym)) {
        errorDuplicateDefinition(qualifiedName, SymbolKind::Enum, {});
    }
}