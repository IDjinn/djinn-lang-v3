//
// Import statement processing
//

#include "../Binder.h"

void Binder::processImports(const Program &program) const {
    // Create aliases for symbols in the file's own namespace
    if (!program.fileNamespace.empty()) {
        const std::string filePrefix = program.fileNamespace + "::";
        for (const auto &[name, symbol]: _global_scope->symbols()) {
            if (name.starts_with(filePrefix)) {
                const std::string shortName = name.substr(filePrefix.length());
                // Only alias if it's a direct member (no nested ::)
                if (shortName.find("::") == std::string::npos) {
                    if (!_global_scope->isDefinedLocally(shortName)) {
                        _global_scope->defineAlias(shortName, symbol);
                    }
                }
            }
        }
    }

    // Process explicit imports
    for (const auto &import: program.imports) {
        const std::string nsPath = import->namespacePath.toString();

        for (const auto &[name, symbol]: _global_scope->symbols()) {
            if (name.starts_with(nsPath + "::")) {
                if (const std::string shortName = name.substr(nsPath.length() + 2);
                    shortName.find("::") == std::string::npos) {
                    if (!_global_scope->isDefinedLocally(shortName)) {
                        _global_scope->defineAlias(shortName, symbol);
                    }
                }
            }
        }
    }
}