//
// Declaration collector dispatcher - routes to specific collectors
//

#include "../Binder.h"

void Binder::collectDeclarations(const Program &program) {
    const std::string filePrefix = program.fileNamespace;

    for (const auto &ext: program.externFunctions) {
        collectExternFunction(*ext);
    }

    for (const auto &iface: program.interfaces) {
        collectInterfaceWithPrefix(*iface, filePrefix);
    }

    for (const auto &enumDecl: program.enums) {
        collectEnumWithPrefix(*enumDecl, filePrefix);
    }

    for (const auto &struc: program.structs) {
        collectStructWithPrefix(*struc, filePrefix);
    }

    for (const auto &func: program.functions) {
        collectFunctionWithPrefix(*func, filePrefix);
    }

    for (const auto &ns: program.namespaces) {
        collectNamespace(*ns, "");
    }
}