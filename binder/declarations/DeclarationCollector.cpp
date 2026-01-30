//
// Declaration collector dispatcher - routes to specific collectors via visitor pattern
//

#include "../Binder.h"
#include "../visitors/DeclarationCollectorVisitor.h"

void Binder::collectDeclarations(const Program &program) const {
    djinn::DeclarationCollectorVisitor collector(*this);
    program.acceptAll(collector);
}