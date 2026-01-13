//
// Enum declaration collection
//

#include "../Binder.h"

void Binder::collectEnum(const EnumDeclaration &decl) const {
    collectEnumWithPrefix(decl, "");
}

void Binder::collectEnumWithPrefix(const EnumDeclaration &decl, const std::string &prefix) const {
    const std::string qualifiedName = prefix.empty() ? decl.name.token_name : prefix + "::" + decl.name.token_name;
    const auto enumSym = std::make_shared<EnumSymbol>(qualifiedName);

    // Collect generic parameters
    for (const auto &genParam: decl.genericParams.params) {
        enumSym->addGenericParam(genParam.name.token_name);
    }

    for (const auto &variant: decl.values) {
        if (enumSym->hasVariant(variant.name.token_name)) {
            errorDuplicateDefinition(variant.name.token_name, SymbolKind::Enum, {});
        } else {
            enumSym->addVariant(variant.name.token_name, variant.types);
        }
    }

    if (!_global_scope->defineEnum(enumSym)) {
        errorDuplicateDefinition(qualifiedName, SymbolKind::Enum, {});
    }
}