//
// Control flow statements: if, for, while, do-while, switch
//

#include "../Binder.h"

void Binder::bindIfStatement(const IfStatement &stmt) {
    bindExpression(*stmt.condition);

    pushScope();
    if (stmt.thenBranch) {
        bindBlock(*stmt.thenBranch);
    }
    popScope();

    if (stmt.elseBranch) {
        pushScope();
        bindBlock(*stmt.elseBranch);
        popScope();
    }
}

void Binder::bindForStatement(const ForStatement &stmt) {
    pushScope();
    loopDepth_++;

    if (stmt.initializer) {
        bindExpression(*stmt.initializer);
    }
    if (stmt.condition) {
        bindExpression(*stmt.condition);
    }
    if (stmt.postfix) {
        bindExpression(*stmt.postfix);
    }
    if (stmt.body) {
        bindBlock(*stmt.body);
    }

    loopDepth_--;
    popScope();
}

void Binder::bindWhileStatement(const WhileStatement &stmt) {
    bindExpression(*stmt.condition);

    pushScope();
    loopDepth_++;

    if (stmt.body) {
        bindBlock(*stmt.body);
    }

    loopDepth_--;
    popScope();
}

void Binder::bindDoWhileStatement(const DoWhileStatement &stmt) {
    pushScope();
    loopDepth_++;

    if (stmt.body) {
        bindBlock(*stmt.body);
    }

    loopDepth_--;
    popScope();

    bindExpression(*stmt.condition);
}

void Binder::bindSwitchStatement(const SwitchStatement &stmt) {
    bindExpression(*stmt.value);

    switchDepth_++;

    for (const auto &caseStmt: stmt.cases) {
        if (caseStmt->expression) {
            bindExpression(*caseStmt->expression);
        }
        if (caseStmt->body) {
            pushScope();
            bindBlock(*caseStmt->body);
            popScope();
        }
    }

    switchDepth_--;
}