#include "BodyAnalyzer.h"

#include <set>

#include "../binder/Symbol.h"
#include "../parser/ast/Statement.h"

namespace djinn::verification
{
    namespace
    {
        class BodyWalker
        {
        public:
            BodyWalker(const CallSiteVisitor& on_call, const PostconditionQuery& postcondition,
                       BodySemantics* semantics, const std::vector<const ContractClause*>& seeds)
                : _on_call(on_call), _postcondition(postcondition), _semantics(semantics), _seeds(seeds)
            {
            }

            void seed_requires(const std::vector<const ContractClause*>& contracts)
            {
                for (const auto* clause : contracts)
                {
                    if (!clause || !clause->isRequire() || !clause->condition) continue;
                    if (const auto condition = translate_condition(*clause->condition))
                        _facts.assume(*condition);
                }
                // Extra assumptions (entity invariants on methods).
                for (const auto* clause : _seeds)
                {
                    if (!clause || !clause->condition) continue;
                    if (const auto condition = translate_condition(*clause->condition))
                        _facts.assume(*condition);
                }
            }

            BodySummary walk_block_scoped(const Block& block)
            {
                walk_block(block);
                return BodySummary{std::move(_returns)};
            }

            [[nodiscard]] FactSet facts() const { return _facts; }

        private:
            const CallSiteVisitor& _on_call;
            const PostconditionQuery& _postcondition;
            BodySemantics* _semantics;
            const std::vector<const ContractClause*>& _seeds;
            FactSet _facts;
            std::vector<ReturnPath> _returns;

            void note_read(const Expression& object, const SourceIdentifier& field)
            {
                if (!_semantics) return;
                if (const auto* identifier = dynamic_cast<const Identifier*>(&object))
                    _semantics->reads.insert(identifier->name() + "." + field.token_name);
            }

            void note_write(const Expression& object, const SourceIdentifier& field)
            {
                if (!_semantics) return;
                if (const auto* identifier = dynamic_cast<const Identifier*>(&object))
                    _semantics->writes.insert(identifier->name() + "." + field.token_name);
            }

            bool walk_block(const Block& block)
            {
                for (const auto& statement : block.statements)
                {
                    if (statement && walk_statement(*statement)) return true;
                }
                return false;
            }

            // Returns true when the statement definitely terminates the flow
            // (return / throw / break / continue / both branches terminate).
            bool walk_statement(const Statement& statement)
            {
                if (const auto* block = dynamic_cast<const Block*>(&statement))
                    return walk_block(*block);

                if (const auto* expression_statement = dynamic_cast<const ExpressionStatement*>(&statement))
                {
                    if (const auto* assignment = dynamic_cast<const Assignment*>(
                            expression_statement->expression.get()))
                    {
                        walk_assigned_value(assignment->name.token_name, *assignment->value);
                        return false;
                    }
                    if (const auto* init = dynamic_cast<const VariableInit*>(
                            expression_statement->expression.get()))
                    {
                        // A declaration may shadow an outer binding: the
                        // target starts invalidated in every case.
                        _facts.kill(init->name.token_name);
                        walk_assigned_value(init->name.token_name, *init->value);
                        return false;
                    }
                    walk_expression_calls(*expression_statement->expression);
                    apply_expression_effect(*expression_statement->expression);
                    return false;
                }

                if (const auto* return_statement = dynamic_cast<const ReturnStatement*>(&statement))
                {
                    if (return_statement->value) split_return(*return_statement->value);
                    else record_return(nullptr);
                    return true;
                }

                if (dynamic_cast<const ThrowStatement*>(&statement)) return true;
                if (dynamic_cast<const BreakStatement*>(&statement)) return true;
                if (dynamic_cast<const ContinueStatement*>(&statement)) return true;

                if (const auto* if_statement = dynamic_cast<const IfStatement*>(&statement))
                    return walk_if(*if_statement);

                if (const auto* while_statement = dynamic_cast<const WhileStatement*>(&statement))
                {
                    walk_loop_body(while_statement->body.get(), while_statement->condition.get(), nullptr);
                    return false;
                }

                if (const auto* do_while = dynamic_cast<const DoWhileStatement*>(&statement))
                {
                    walk_loop_body(do_while->body.get(), do_while->condition.get(), nullptr);
                    return false;
                }

                if (const auto* for_statement = dynamic_cast<const ForStatement*>(&statement))
                {
                    if (for_statement->initializer)
                    {
                        walk_expression_calls(*for_statement->initializer);
                        apply_expression_effect(*for_statement->initializer);
                    }
                    walk_loop_body(for_statement->body.get(), for_statement->condition.get(),
                                   for_statement->postfix.get());
                    return false;
                }

                if (const auto* range_for = dynamic_cast<const RangeForStatement*>(&statement))
                {
                    _facts.kill(range_for->variableName.token_name);
                    walk_loop_body(range_for->body.get(), nullptr, nullptr);
                    return false;
                }

                if (const auto* try_catch = dynamic_cast<const TryCatchStatement*>(&statement))
                    return walk_try_catch(*try_catch);

                if (const auto* switch_statement = dynamic_cast<const SwitchStatement*>(&statement))
                {
                    walk_switch(*switch_statement);
                    return false;
                }

                if (const auto* yield = dynamic_cast<const YieldStatement*>(&statement))
                {
                    if (yield->value) walk_expression_calls(*yield->value);
                    return false;
                }

                if (const auto* spawn = dynamic_cast<const SpawnStatement*>(&statement))
                {
                    if (spawn->expression) walk_expression_calls(*spawn->expression);
                    return false;
                }

                return false;
            }

            bool walk_if(const IfStatement& statement)
            {
                if (statement.condition) walk_expression_calls(*statement.condition);
                const auto condition = statement.condition ? translate_condition(*statement.condition) : nullptr;

                const FactSet base = _facts;

                _facts = base;
                if (condition) _facts.assume(*condition, true);
                const bool then_terminated = statement.thenBranch ? walk_block(*statement.thenBranch) : false;
                const FactSet after_then = _facts;

                _facts = base;
                if (condition) _facts.assume(*condition, false);
                const bool else_terminated = statement.elseBranch ? walk_block(*statement.elseBranch) : false;
                const FactSet after_else = _facts;

                if (then_terminated && else_terminated) return true;
                if (then_terminated)
                {
                    _facts = after_else;
                    return false;
                }
                if (else_terminated)
                {
                    _facts = after_then;
                    return false;
                }
                _facts = after_then;
                _facts.join(after_else);
                return false;
            }

            // Shared loop shape: everything the loop assigns becomes Unknown,
            // the body is walked once for its return paths (with the loop
            // condition assumed on entry), and control continues afterwards
            // with the weakened facts.
            void walk_loop_body(const Block* body, const Expression* condition, const Expression* postfix)
            {
                std::set<std::string> assigned;
                if (body) collect_assigned(*body, assigned);
                if (postfix) collect_expression_targets(*postfix, assigned);
                for (const auto& name : assigned) _facts.kill(name);

                const FactSet base = _facts;
                if (condition)
                {
                    walk_expression_calls(*condition);
                    if (const auto translated = translate_condition(*condition))
                        _facts.assume(*translated, true);
                }
                if (postfix) walk_expression_calls(*postfix);
                if (body) walk_block(*body);
                _facts = base;
            }

            bool walk_try_catch(const TryCatchStatement& statement)
            {
                const FactSet base = _facts;

                std::set<std::string> try_assigned;
                if (statement.tryBlock) collect_assigned(*statement.tryBlock, try_assigned);
                // A catch arm may run after a partial try execution, so the
                // catch enters with the try's assignments invalidated.
                FactSet catch_entry = base;
                for (const auto& name : try_assigned) catch_entry.kill(name);

                _facts = catch_entry;
                const bool try_terminated = statement.tryBlock ? walk_block(*statement.tryBlock) : false;
                const FactSet after_try = _facts;

                std::optional<FactSet> joined;
                if (!try_terminated) joined = after_try;
                bool all_terminated = try_terminated;

                for (const auto& clause : statement.catches)
                {
                    _facts = catch_entry;
                    const bool catch_terminated = clause.body ? walk_block(*clause.body) : false;
                    if (catch_terminated) continue;
                    all_terminated = false;
                    if (joined) joined->join(_facts);
                    else joined = _facts;
                }

                if (all_terminated) return true;

                _facts = joined ? *joined : catch_entry;
                if (statement.finallyBlock) walk_block(*statement.finallyBlock);
                return false;
            }

            void walk_switch(const SwitchStatement& statement)
            {
                std::set<std::string> assigned;
                for (const auto& arm : statement.cases)
                {
                    if (arm && arm->body) collect_assigned(*arm->body, assigned);
                }
                for (const auto& name : assigned) _facts.kill(name);

                const FactSet base = _facts;
                for (const auto& arm : statement.cases)
                {
                    if (!arm) continue;
                    if (arm->expression) walk_expression_calls(*arm->expression);
                    _facts = base;
                    if (arm->body) walk_block(*arm->body);
                }
                _facts = base;
            }

            // A ternary in return position contributes two return paths.
            void split_return(const Expression& value)
            {
                if (const auto* ternary = dynamic_cast<const TernaryExpression*>(&value))
                {
                    if (ternary->condition) walk_expression_calls(*ternary->condition);
                    const auto condition = ternary->condition ? translate_condition(*ternary->condition) : nullptr;
                    const FactSet base = _facts;

                    _facts = base;
                    if (condition) _facts.assume(*condition, true);
                    split_return(*ternary->trueExpr);

                    _facts = base;
                    if (condition) _facts.assume(*condition, false);
                    split_return(*ternary->falseExpr);

                    _facts = base;
                    return;
                }
                walk_expression_calls(value);
                record_return(&value);
            }

            void record_return(const Expression* value)
            {
                ReturnPath path;
                path.facts = _facts;
                if (value) path.value = translate_linear(*value);
                _returns.push_back(std::move(path));
            }

            void apply_expression_effect(const Expression& expression)
            {
                if (const auto* init = dynamic_cast<const VariableInit*>(&expression))
                {
                    // A declaration may shadow an outer binding: invalidate
                    // whatever was known about the name first.
                    _facts.kill(init->name.token_name);
                    if (const auto value = translate_linear(*init->value))
                        _facts.set_equality(init->name.token_name, *value);
                    return;
                }

                if (const auto* declaration = dynamic_cast<const VariableDeclaration*>(&expression))
                {
                    _facts.kill(declaration->name.token_name);
                    return;
                }

                if (const auto* assignment = dynamic_cast<const Assignment*>(&expression))
                {
                    if (const auto value = translate_linear(*assignment->value))
                        _facts.set_equality(assignment->name.token_name, *value);
                    else
                        _facts.kill(assignment->name.token_name);
                    return;
                }

                if (const auto* postfix = dynamic_cast<const PostfixExpression*>(&expression))
                {
                    if (const auto* operand = dynamic_cast<const Identifier*>(postfix->operand.get()))
                    {
                        const int64_t delta = postfix->op == TokenType::PLUS_PLUS ? 1 : -1;
                        Affine updated = Affine::of_variable(operand->name());
                        if (const auto stepped = updated.plus(Affine::of_constant(delta)))
                            _facts.set_equality(operand->name(), *stepped);
                        else
                            _facts.kill(operand->name());
                    }
                    return;
                }

                if (const auto* field_assignment = dynamic_cast<const FieldAssignment*>(&expression))
                {
                    note_write(*field_assignment->object, field_assignment->fieldName);
                    // `object.field = value`: the field is a tracked variable
                    // ("this.balance"), so the assignment keeps its equality
                    // when the value is affine (self-references included).
                    if (const auto* object = dynamic_cast<const Identifier*>(field_assignment->object.get()))
                    {
                        const std::string target =
                            object->name() + "." + field_assignment->fieldName.token_name;
                        if (const auto value = translate_linear(*field_assignment->value))
                            _facts.set_equality(target, *value);
                        else
                            _facts.kill(target);
                    }
                    return;
                }

                if (const auto* index_assignment = dynamic_cast<const IndexAssignment*>(&expression))
                {
                    if (const auto* object = dynamic_cast<const Identifier*>(index_assignment->object.get()))
                        _facts.kill(object->name());
                    return;
                }
            }

            void collect_assigned(const Block& block, std::set<std::string>& out) const
            {
                for (const auto& statement : block.statements)
                {
                    if (statement) collect_assigned_statement(*statement, out);
                }
            }

            void collect_assigned_statement(const Statement& statement, std::set<std::string>& out) const
            {
                if (const auto* block = dynamic_cast<const Block*>(&statement))
                {
                    collect_assigned(*block, out);
                    return;
                }
                if (const auto* expression_statement = dynamic_cast<const ExpressionStatement*>(&statement))
                {
                    collect_expression_targets(*expression_statement->expression, out);
                    return;
                }
                if (const auto* if_statement = dynamic_cast<const IfStatement*>(&statement))
                {
                    if (if_statement->thenBranch) collect_assigned(*if_statement->thenBranch, out);
                    if (if_statement->elseBranch) collect_assigned(*if_statement->elseBranch, out);
                    return;
                }
                if (const auto* while_statement = dynamic_cast<const WhileStatement*>(&statement))
                {
                    if (while_statement->body) collect_assigned(*while_statement->body, out);
                    return;
                }
                if (const auto* do_while = dynamic_cast<const DoWhileStatement*>(&statement))
                {
                    if (do_while->body) collect_assigned(*do_while->body, out);
                    return;
                }
                if (const auto* for_statement = dynamic_cast<const ForStatement*>(&statement))
                {
                    if (for_statement->initializer)
                        collect_expression_targets(*for_statement->initializer, out);
                    if (for_statement->postfix) collect_expression_targets(*for_statement->postfix, out);
                    if (for_statement->body) collect_assigned(*for_statement->body, out);
                    return;
                }
                if (const auto* range_for = dynamic_cast<const RangeForStatement*>(&statement))
                {
                    out.insert(range_for->variableName.token_name);
                    if (range_for->body) collect_assigned(*range_for->body, out);
                    return;
                }
                if (const auto* try_catch = dynamic_cast<const TryCatchStatement*>(&statement))
                {
                    if (try_catch->tryBlock) collect_assigned(*try_catch->tryBlock, out);
                    for (const auto& clause : try_catch->catches)
                        if (clause.body) collect_assigned(*clause.body, out);
                    if (try_catch->finallyBlock) collect_assigned(*try_catch->finallyBlock, out);
                    return;
                }
                if (const auto* switch_statement = dynamic_cast<const SwitchStatement*>(&statement))
                {
                    for (const auto& arm : switch_statement->cases)
                        if (arm && arm->body) collect_assigned(*arm->body, out);
                    return;
                }
            }

            void collect_expression_targets(const Expression& expression, std::set<std::string>& out) const
            {
                if (const auto* init = dynamic_cast<const VariableInit*>(&expression))
                {
                    out.insert(init->name.token_name);
                    collect_expression_targets(*init->value, out);
                    return;
                }
                if (const auto* declaration = dynamic_cast<const VariableDeclaration*>(&expression))
                {
                    out.insert(declaration->name.token_name);
                    return;
                }
                if (const auto* assignment = dynamic_cast<const Assignment*>(&expression))
                {
                    out.insert(assignment->name.token_name);
                    collect_expression_targets(*assignment->value, out);
                    return;
                }
                if (const auto* postfix = dynamic_cast<const PostfixExpression*>(&expression))
                {
                    if (const auto* operand = dynamic_cast<const Identifier*>(postfix->operand.get()))
                        out.insert(operand->name());
                    return;
                }
                if (const auto* field_assignment = dynamic_cast<const FieldAssignment*>(&expression))
                {
                    if (const auto* object = dynamic_cast<const Identifier*>(field_assignment->object.get()))
                        out.insert(object->name() + "." + field_assignment->fieldName.token_name);
                    return;
                }
                if (const auto* index_assignment = dynamic_cast<const IndexAssignment*>(&expression))
                {
                    if (const auto* object = dynamic_cast<const Identifier*>(index_assignment->object.get()))
                        out.insert(object->name());
                    return;
                }
                if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expression))
                {
                    collect_expression_targets(*binary->left, out);
                    collect_expression_targets(*binary->right, out);
                    return;
                }
                if (const auto* ternary = dynamic_cast<const TernaryExpression*>(&expression))
                {
                    collect_expression_targets(*ternary->trueExpr, out);
                    collect_expression_targets(*ternary->falseExpr, out);
                    return;
                }
            }

            // `target = <value>` / `let target = <value>` statement form:
            // walk the value for its call sites, then keep the target's
            // equality when the value (call results included) is affine.
            void walk_assigned_value(const std::string& target, const Expression& value)
            {
                walk_expression_calls(value);
                _facts.kill(target);
                if (const auto affine = call_result_affine(value))
                    _facts.set_equality(target, *affine);
            }

            // Affine value of an expression whose FunctionCall leaves stand
            // for the callees' proven postconditions. Everything the plain
            // fragment translates stays identical to translate_linear.
            std::optional<Affine> call_result_affine(const Expression& expression) const
            {
                if (const auto* call = dynamic_cast<const FunctionCall*>(&expression))
                    return _postcondition ? _postcondition(*call) : std::nullopt;

                if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expression))
                {
                    const auto left = call_result_affine(*binary->left);
                    const auto right = call_result_affine(*binary->right);
                    if (!left || !right) return std::nullopt;
                    switch (binary->op)
                    {
                        case TokenType::PLUS:
                            return left->plus(*right);
                        case TokenType::MINUS:
                            return left->plus(*right, -1);
                        case TokenType::STAR:
                            if (const auto factor = left->as_constant())
                                return right->scaled(*factor);
                            if (const auto factor = right->as_constant())
                                return left->scaled(*factor);
                            return std::nullopt;
                        default:
                            return std::nullopt;
                    }
                }

                if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expression))
                {
                    if (unary->op != TokenType::MINUS) return std::nullopt;
                    const auto operand = call_result_affine(*unary->operand);
                    if (!operand) return std::nullopt;
                    return operand->negated();
                }

                return translate_linear(expression);
            }

            void walk_expression_calls(const Expression& expression)
            {
                if (const auto* call = dynamic_cast<const FunctionCall*>(&expression))
                {
                    if (_on_call) _on_call(*call, _facts);
                    if (_semantics && !call->resolvedCalleeName.empty())
                        _semantics->calls.push_back(call->resolvedCalleeName);
                    for (const auto& argument : call->arguments)
                        walk_expression_calls(*argument);
                    if (call->receiver) walk_expression_calls(*call->receiver);
                    return;
                }

                // Assignments nested inside a larger expression (conditions,
                // call arguments) invalidate the facts of their target: the
                // precise update is applied by the statement-level walker.
                if (const auto* assignment = dynamic_cast<const Assignment*>(&expression))
                {
                    _facts.kill(assignment->name.token_name);
                    walk_expression_calls(*assignment->value);
                    return;
                }

                if (const auto* init = dynamic_cast<const VariableInit*>(&expression))
                {
                    _facts.kill(init->name.token_name);
                    walk_expression_calls(*init->value);
                    return;
                }

                if (const auto* postfix = dynamic_cast<const PostfixExpression*>(&expression))
                {
                    if (const auto* operand = dynamic_cast<const Identifier*>(postfix->operand.get()))
                        _facts.kill(operand->name());
                    return;
                }

                if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expression))
                {
                    walk_expression_calls(*binary->left);
                    walk_expression_calls(*binary->right);
                    return;
                }

                if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expression))
                {
                    walk_expression_calls(*unary->operand);
                    return;
                }

                if (const auto* postfix = dynamic_cast<const PostfixExpression*>(&expression))
                {
                    walk_expression_calls(*postfix->operand);
                    return;
                }

                if (const auto* ternary = dynamic_cast<const TernaryExpression*>(&expression))
                {
                    walk_expression_calls(*ternary->condition);
                    walk_expression_calls(*ternary->trueExpr);
                    walk_expression_calls(*ternary->falseExpr);
                    return;
                }

                if (const auto* field_access = dynamic_cast<const FieldAccess*>(&expression))
                {
                    note_read(*field_access->object, field_access->fieldName);
                    walk_expression_calls(*field_access->object);
                    return;
                }

                if (const auto* field_assignment = dynamic_cast<const FieldAssignment*>(&expression))
                {
                    walk_expression_calls(*field_assignment->object);
                    walk_expression_calls(*field_assignment->value);
                    return;
                }

                if (const auto* index_access = dynamic_cast<const IndexAccess*>(&expression))
                {
                    walk_expression_calls(*index_access->object);
                    walk_expression_calls(*index_access->index);
                    return;
                }

                if (const auto* index_assignment = dynamic_cast<const IndexAssignment*>(&expression))
                {
                    walk_expression_calls(*index_assignment->object);
                    walk_expression_calls(*index_assignment->index);
                    walk_expression_calls(*index_assignment->value);
                    return;
                }

                if (const auto* cast = dynamic_cast<const CastExpression*>(&expression))
                {
                    walk_expression_calls(*cast->operand);
                    return;
                }

                if (const auto* is_expression = dynamic_cast<const IsExpression*>(&expression))
                {
                    walk_expression_calls(*is_expression->operand);
                    return;
                }

                if (const auto* await = dynamic_cast<const AwaitExpression*>(&expression))
                {
                    walk_expression_calls(*await->operand);
                    return;
                }

                if (const auto* try_expression = dynamic_cast<const TryExpression*>(&expression))
                {
                    walk_expression_calls(*try_expression->expr);
                    walk_expression_calls(*try_expression->fallback);
                    return;
                }

                if (const auto* brace = dynamic_cast<const BraceInitializer*>(&expression))
                {
                    for (const auto& element : brace->elements)
                        walk_expression_calls(*element.value);
                    return;
                }

                if (const auto* array_literal = dynamic_cast<const ArrayLiteral*>(&expression))
                {
                    for (const auto& element : array_literal->elements)
                        walk_expression_calls(*element);
                    return;
                }

                if (const auto* new_expression = dynamic_cast<const NewExpression*>(&expression))
                {
                    if (new_expression->constructorCall)
                        walk_expression_calls(*new_expression->constructorCall);
                    return;
                }
            }
        };
    }

    BodySummary analyze_function_body(const FunctionSymbol& function, const CallSiteVisitor& on_call,
                                      const PostconditionQuery& postcondition, BodySemantics* semantics,
                                      const std::vector<const ContractClause*>& seed_contracts)
    {
        if (!function.body) return {};
        BodyWalker walker(on_call, postcondition, semantics, seed_contracts);
        walker.seed_requires(function.contracts);
        return walker.walk_block_scoped(*function.body);
    }

    BodySummary analyze_method_body(const MethodSymbol& method, const CallSiteVisitor& on_call,
                                    const PostconditionQuery& postcondition, BodySemantics* semantics,
                                    const std::vector<const ContractClause*>& seed_contracts)
    {
        BodyWalker walker(on_call, postcondition, semantics, seed_contracts);
        walker.seed_requires(method.contracts);

        if (method.body) return walker.walk_block_scoped(*method.body);

        // Expression-bodied method: a single return path over the expression.
        if (method.expressionBody)
        {
            BodySummary summary;
            ReturnPath path;
            path.facts = walker.facts();
            path.value = translate_linear(*method.expressionBody);
            summary.returns.push_back(std::move(path));
            return summary;
        }
        return {};
    }
}
