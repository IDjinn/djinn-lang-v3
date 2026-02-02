//
// Statement dispatcher - routes to specific binders via visitor pattern
//


#include "../Binder.h"
#include "../visitors/BinderStatementVisitor.h"

void Binder::bindStatement(const Statement &stmt) {
    djinn::BinderStatementVisitor visitor(*this);
    stmt.accept(visitor);
}

void Binder::validateBreakStatement(const BreakStatement &stmt) {
    if (!_controlFlow.canBreak()) {
        BINDER_ERROR(DiagnosticCode::UNEXPECTED_TOKEN,
                     "'break' outside of loop or switch", &stmt, stmt.location);
    }
}

void Binder::validateContinueStatement(const ContinueStatement &stmt) {
    if (!_controlFlow.canContinue()) {
        BINDER_ERROR(DiagnosticCode::UNEXPECTED_TOKEN,
                     "'continue' outside of loop", &stmt, stmt.location);
    }
}

void Binder::bindBlock(const Block &block) {
    for (const auto &stmt: block.statements) {
        bindStatement(*stmt);
    }
}