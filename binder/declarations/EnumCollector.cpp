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

    for (const auto &genParam: decl.genericParams.params) {
        enumSym->addGenericParam(genParam.name.token_name);
    }

    for (const auto &variant: decl.values) {
        if (enumSym->hasVariant(variant.name.token_name)) {
            BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION,
                         "enum variant '" + variant.name.token_name + "' is already defined", variant,
                         variant.name.location);
        } else {
            enumSym->addVariant(variant.name.token_name, variant.types);
        }
    }

    if (!_global_scope->defineEnum(enumSym)) {
        BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION, "enum '" + qualifiedName + "' is already defined", decl,
                     decl.name.location);
    }
}