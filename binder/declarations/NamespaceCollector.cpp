//
// Namespace declaration collection
//

#include "../Binder.h"

void Binder::collectNamespace(const NamespaceDeclaration &ns, const std::string &prefix) const {
    const std::string qualifiedPrefix = prefix.empty() ? ns.name : prefix + "::" + ns.name;

    // Collect structs in namespace
    for (const auto &struc: ns.structs) {
        collectStructWithPrefix(*struc, qualifiedPrefix);
    }

    // Collect functions in namespace
    for (const auto &func: ns.functions) {
        collectFunctionWithPrefix(*func, qualifiedPrefix);
    }

    // Recursively collect nested namespaces
    for (const auto &nestedNs: ns.namespaces) {
        collectNamespace(*nestedNs, qualifiedPrefix);
    }
}