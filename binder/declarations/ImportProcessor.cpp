//
// Import statement processing - uses visitor pattern
//

#include "../Binder.h"
#include "../visitors/ImportProcessorVisitor.h"

void Binder::processImports(const Program &program) const {
    // Copy symbols to avoid modifying map while iterating
    const std::vector<std::pair<std::string, std::shared_ptr<Symbol> > > symbolsCopy(
        _global_scope->symbols().begin(), _global_scope->symbols().end());

    // Handle file namespace aliases
    if (!program.fileNamespace.empty()) {
        const std::string filePrefix = program.fileNamespace + "::";
        for (const auto &[name, symbol]: symbolsCopy) {
            if (!name.starts_with(filePrefix)) continue;

            if (const auto shortName = name.substr(filePrefix.length());
                shortName.find("::") == std::string::npos) {
                if (!_global_scope->isDefinedLocally(shortName)) {
                    _global_scope->defineAlias(shortName, symbol);
                }
            }
        }
    }

    // Auto-import std::types for all programs (prelude)
    {
        const std::string prelude = "std::types::";
        for (const auto& [name, symbol] : symbolsCopy)
        {
            if (!name.starts_with(prelude)) continue;
            const auto shortName = name.substr(prelude.length());
            if (shortName.find("::") != std::string::npos) continue;
            if (!_global_scope->isDefinedLocally(shortName))
            {
                _global_scope->defineAlias(shortName, symbol);
            }
        }
    }

    // Process explicit imports via visitor
    djinn::ImportProcessorVisitor visitor(*this, symbolsCopy);
    for (const auto &import: program.imports) {
        import->accept(visitor, program.fileNamespace);
    }
}