//
// Expression dispatcher - routes to specific binders via visitor pattern
//

#include "../Binder.h"
#include "../visitors/BinderExpressionVisitor.h"

std::shared_ptr<Symbol> Binder::bindExpression(const Expression &expr) {
    djinn::BinderExpressionVisitor visitor(*this);
    expr.accept(visitor);
    return visitor.result();
}