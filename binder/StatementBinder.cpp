//
// Created by Claude on 05/01/2026.
//

#include "Binder.h"

void Binder::bindProgram(const Program &program) {
    for (const auto &struc: program.structs) {
        for (const auto &field: struc->fields) {
            if (!isTypeDefined(*field.type)) {
                if (field.type->kind == TypeKind::STRUCT) {
                    if (struc->genericParams.find(field.type->structName) == nullptr) {
                        errorUndefinedStruct(field.type->structName, {});
                    }
                }
            }
        }
        // Bind struct methods
        for (const auto &method: struc->methods) {
            bindMethod(*method, *struc);
        }
    }

    for (const auto &func: program.functions) {
        bindFunction(*func);
    }

    for (const auto &ns: program.namespaces) {
        bindNamespace(*ns, "");
    }
}

void Binder::bindFunction(const FunctionDeclaration &func) {
    currentFunction_ = func.name;

    pushScope();

    for (const auto &param: func.parameters) {
        // Validate parameter type
        if (!isTypeDefined(*param.type)) {
            if (param.type->kind == TypeKind::STRUCT) {
                errorUndefinedStruct(param.type->structName, {});
            }
        }

        if (!_current_scope->defineParameter(param.name, *param.type, param.isMutable)) {
            errorDuplicateDefinition(param.name, SymbolKind::Parameter, {});
        }
    }

    if (!isTypeDefined(*func.returnType)) {
        if (func.returnType->kind == TypeKind::STRUCT) {
            errorUndefinedStruct(func.returnType->structName, {});
        }
    }

    if (func.body) {
        bindBlock(*func.body);
    }

    popScope();
    currentFunction_.clear();
}

void Binder::bindMethod(const StructMethodDeclaration &method, const StructDeclaration &struc) {
    currentFunction_ = struc.name + "::" + method.name;

    pushScope();

    // Define 'this' as pointer to struct type (fields accessed via this.fieldName)
    Type thisType;
    thisType.kind = TypeKind::POINTER;
    thisType.elementType = std::make_unique<Type>();
    thisType.elementType->kind = TypeKind::STRUCT;
    thisType.elementType->structName = struc.name;
    _current_scope->defineVariable("this", thisType, false);

    // Add method parameters
    for (const auto &param: method.parameters) {
        if (!isTypeDefined(*param.type)) {
            if (param.type->kind == TypeKind::STRUCT) {
                errorUndefinedStruct(param.type->structName, {});
            }
        }
        if (!_current_scope->defineParameter(param.name, *param.type, param.isMutable)) {
            errorDuplicateDefinition(param.name, SymbolKind::Parameter, {});
        }
    }

    // Validate return type
    if (!isTypeDefined(*method.returnType)) {
        if (method.returnType->kind == TypeKind::STRUCT) {
            errorUndefinedStruct(method.returnType->structName, {});
        }
    }

    // Bind method body or expression
    if (method.body) {
        bindBlock(*method.body);
    } else if (method.expression) {
        bindExpression(*method.expression);
    }

    popScope();
    currentFunction_.clear();
}

void Binder::bindBlock(const Block &block) {
    for (const auto &stmt: block.statements) {
        bindStatement(*stmt);
    }
}

void Binder::bindNamespace(const NamespaceDeclaration &ns, const std::string &prefix) {
    const std::string qualifiedPrefix = prefix.empty() ? ns.name : prefix + "::" + ns.name;

    for (const auto &struc: ns.structs) {
        for (const auto &field: struc->fields) {
            if (!isTypeDefined(*field.type)) {
                if (field.type->kind == TypeKind::STRUCT) {
                    if (struc->genericParams.find(field.type->structName) == nullptr) {
                        errorUndefinedStruct(field.type->structName, {});
                    }
                }
            }
        }
        // Bind struct methods
        for (const auto &method: struc->methods) {
            bindMethod(*method, *struc);
        }
    }

    for (const auto &func: ns.functions) {
        bindFunction(*func);
    }

    for (const auto &nestedNs: ns.namespaces) {
        bindNamespace(*nestedNs, qualifiedPrefix);
    }
}

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