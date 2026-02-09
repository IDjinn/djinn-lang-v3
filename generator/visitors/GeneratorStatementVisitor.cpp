//
// Generator Statement Visitor Implementation
//

#include "GeneratorStatementVisitor.h"
#include "../Generator.h"
#include "../../parser/ast/Statement.h"

namespace djinn {
    void GeneratorStatementVisitor::visit(const ExpressionStatement &stmt) {
        _generator.generate_expression(*stmt.expression);
    }

    void GeneratorStatementVisitor::visit(const ReturnStatement &stmt) {
        _generator.generate_return_statement(stmt);
    }

    void GeneratorStatementVisitor::visit(const Block &stmt) {
        _generator.generate_block(stmt);
    }

    void GeneratorStatementVisitor::visit(const IfStatement &stmt) {
        _generator.generate_if_statement(stmt);
    }

    void GeneratorStatementVisitor::visit(const ForStatement &stmt) {
        _generator.generate_for_statement(stmt);
    }

    void GeneratorStatementVisitor::visit(const WhileStatement &stmt) {
        _generator.generate_while_statement(stmt);
    }

    void GeneratorStatementVisitor::visit(const DoWhileStatement &stmt) {
        _generator.generate_do_while_statement(stmt);
    }

    void GeneratorStatementVisitor::visit(const SwitchStatement &stmt) {
        _generator.generate_switch_statement(stmt);
    }

    void GeneratorStatementVisitor::visit(const BreakStatement &/*stmt*/) {
        _generator.generate_break_statement();
    }

    void GeneratorStatementVisitor::visit(const ContinueStatement &/*stmt*/) {
        _generator.generate_continue_statement();
    }
} // namespace djinn