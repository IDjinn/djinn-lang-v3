//
// Statement dispatcher - routes to specific binders
//

#include "../Binder.h"

void Binder::bindStatement(const Statement &stmt) {
    if (const auto *exprStmt = dynamic_cast<const ExpressionStatement *>(&stmt)) {
        bindExpression(*exprStmt->expression);
    } else if (const auto *retStmt = dynamic_cast<const ReturnStatement *>(&stmt)) {
        if (retStmt->value) {
            bindExpression(*retStmt->value);
        }
    } else if (const auto *blockStmt = dynamic_cast<const Block *>(&stmt)) {
        pushScope();
        bindBlock(*blockStmt);
        popScope();
    } else if (const auto *ifStmt = dynamic_cast<const IfStatement *>(&stmt)) {
        bindIfStatement(*ifStmt);
    } else if (const auto *forStmt = dynamic_cast<const ForStatement *>(&stmt)) {
        bindForStatement(*forStmt);
    } else if (const auto *whileStmt = dynamic_cast<const WhileStatement *>(&stmt)) {
        bindWhileStatement(*whileStmt);
    } else if (const auto *doWhileStmt = dynamic_cast<const DoWhileStatement *>(&stmt)) {
        bindDoWhileStatement(*doWhileStmt);
    } else if (const auto *switchStmt = dynamic_cast<const SwitchStatement *>(&stmt)) {
        bindSwitchStatement(*switchStmt);
    } else if (dynamic_cast<const BreakStatement *>(&stmt)) {
        if (loopDepth_ == 0 && switchDepth_ == 0) {
            _diagnostics.error(DiagnosticCode::UNEXPECTED_TOKEN,
                               "'break' outside of loop or switch", {});
        }
    } else if (dynamic_cast<const ContinueStatement *>(&stmt)) {
        if (loopDepth_ == 0) {
            _diagnostics.error(DiagnosticCode::UNEXPECTED_TOKEN,
                               "'continue' outside of loop", {});
        }
    }
}

void Binder::bindBlock(const Block &block) {
    for (const auto &stmt: block.statements) {
        bindStatement(*stmt);
    }
}