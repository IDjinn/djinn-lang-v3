//
// Import Processor Visitor Implementation
//

#include "ImportProcessorVisitor.h"
#include "../Binder.h"
#include "../../parser/ast/Declaration.h"

namespace djinn {
    void ImportProcessorVisitor::visit(const ImportDeclaration &decl, const std::string &prefix) {
        const auto nsPath = decl.namespacePath.toString();

        for (const auto &[name, symbol]: _symbolsCopy) {
            if (!name.starts_with(nsPath + "::")) continue;

            if (const auto shortName = name.substr(nsPath.length() + 2);
                shortName.find("::") == std::string::npos &&
                !_binder._global_scope->isDefinedLocally(shortName)) {
                _binder._global_scope->defineAlias(shortName, symbol);
            }
        }
    }
} // namespace djinn