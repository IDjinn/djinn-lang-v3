//
// Import statement processing
//

#include "../Binder.h"

void Binder::processImports(const Program &program) const {
    if (!program.fileNamespace.empty()) {
        const std::string filePrefix = program.fileNamespace + "::";
        for (const auto &[name, symbol]: _global_scope->symbols()) {
            if (!name.starts_with(filePrefix)) continue;

            if (const auto shortName = name.substr(filePrefix.length()); shortName.find("::") == std::string::npos) {
                if (!_global_scope->isDefinedLocally(shortName)) {
                    _global_scope->defineAlias(shortName, symbol);
                }
            }
        }
    }

    for (const auto &import: program.imports) {
        const auto nsPath = import->namespacePath.toString();
        for (const auto &[name, symbol]: _global_scope->symbols()) {
            if (!name.starts_with(nsPath + "::")) continue;

            if (const auto shortName = name.substr(nsPath.length() + 2);
                shortName.find("::") == std::string::npos && !_global_scope->isDefinedLocally(shortName)) {
                _global_scope->defineAlias(shortName, symbol);
            }
        }
    }
}
