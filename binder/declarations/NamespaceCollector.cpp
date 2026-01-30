//
// Namespace declaration collection - uses visitor pattern for nested declarations
//

#include "../Binder.h"
#include "../visitors/DeclarationCollectorVisitor.h"

void Binder::collectNamespace(const NamespaceDeclaration &ns, const std::string &prefix) const {
    const std::string qualifiedPrefix = prefix.empty() ? ns.name.token_name : prefix + "::" + ns.name.token_name;

    djinn::DeclarationCollectorVisitor collector(*this);

    for (const auto &struc: ns.structs) {
        struc->accept(collector, qualifiedPrefix);
    }

    for (const auto &func: ns.functions) {
        func->accept(collector, qualifiedPrefix);
    }

    for (const auto &nestedNs: ns.namespaces) {
        nestedNs->accept(collector, qualifiedPrefix);
    }
}