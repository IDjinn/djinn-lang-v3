//
// Import Processor Visitor - Processes import declarations
//

#ifndef DJINN_IMPORT_PROCESSOR_VISITOR_H
#define DJINN_IMPORT_PROCESSOR_VISITOR_H

#include "../../visitor/DeclarationVisitor.h"
#include <vector>
#include <memory>

class Binder;
struct Symbol;

namespace djinn {
    class ImportProcessorVisitor : public DeclarationVisitorBase {
        const Binder &_binder;
        const std::vector<std::pair<std::string, std::shared_ptr<Symbol> > > &_symbolsCopy;

    public:
        ImportProcessorVisitor(const Binder &binder,
                               const std::vector<std::pair<std::string, std::shared_ptr<Symbol> > > &symbolsCopy)
            : _binder(binder), _symbolsCopy(symbolsCopy) {
        }

        void visit(const ImportDeclaration &decl, const std::string &prefix) override;
    };
} // namespace djinn

#endif // DJINN_IMPORT_PROCESSOR_VISITOR_H