//
// Namespace declaration collection
//

#include "../Binder.h"

void Binder::collectNamespace(const NamespaceDeclaration &ns, const std::string &prefix) const {
    const std::string qualifiedPrefix = prefix.empty() ? ns.name.token_name : prefix + "::" + ns.name.token_name;

    for (const auto &struc: ns.structs) {
        collectStructWithPrefix(*struc, qualifiedPrefix);
    }

    for (const auto &func: ns.functions) {
        collectFunctionWithPrefix(*func, qualifiedPrefix);
    }

    for (const auto &nestedNs: ns.namespaces) {
        collectNamespace(*nestedNs, qualifiedPrefix);
    }
}