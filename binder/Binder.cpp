//
// Created by Claude on 04/01/2026.
//

#include "Binder.h"

Binder::Binder(DiagnosticEngine &diagnostics)
    : _diagnostics(diagnostics) {
    _global_scope = std::make_shared<ScopedSymbolTable>();
    _current_scope = _global_scope;
}

BindingResult Binder::bind(const Program &program) {
    BindingResult result;
    result.globalScope = _global_scope;

    collectDeclarations(program);
    processImports(program);
    bindProgram(program);

    result.success = !_diagnostics.hasErrors();
    return result;
}

void Binder::pushScope() {
    _current_scope = _current_scope->createChildScope();
}

void Binder::popScope() {
    // Check for unused variables before leaving scope (optional warning)
    // auto unused = currentScope_->getUnusedSymbols();
    // for (const auto& sym : unused) {
    //     diag_.warning(DiagnosticCode::UNUSED_VARIABLE,
    //         "unused " + Symbol::kindToString(sym->kind) + " '" + sym->name + "'",
    //         sym->location);
    // }

    if (const auto parent = _current_scope->parentScope()) {
        _current_scope = parent;
    }
}

void Binder::collectDeclarations(const Program &program) {
    const std::string filePrefix = program.fileNamespace;

    for (const auto &ext: program.externFunctions) {
        collectExternFunction(*ext);
    }

    for (const auto &struc: program.structs) {
        collectStructWithPrefix(*struc, filePrefix);
    }

    for (const auto &func: program.functions) {
        collectFunctionWithPrefix(*func, filePrefix);
    }

    for (const auto &ns: program.namespaces) {
        collectNamespace(*ns, "");
    }
}

void Binder::collectExternFunction(const ExternFunctionDeclaration &decl) const {
    const auto funcSym = std::make_shared<FunctionSymbol>(decl.name, *decl.returnType);
    funcSym->kind = SymbolKind::ExternFunction;
    funcSym->isVariadic = decl.isVariadic;

    for (const auto &param: decl.parameters) {
        funcSym->addParameter(param.name, *param.type);
    }

    if (!_global_scope->defineFunction(funcSym)) {
        errorDuplicateDefinition(decl.name, SymbolKind::ExternFunction, {});
    }
}

void Binder::collectStruct(const StructDeclaration &decl) const {
    const auto structSym = std::make_shared<StructSymbol>(decl.name);

    for (const auto &genParam: decl.genericParams.params) {
        structSym->addGenericParam(genParam.name);
    }

    for (const auto &field: decl.fields) {
        if (structSym->hasField(field.name)) {
            errorDuplicateDefinition(field.name, SymbolKind::Field, {});
        } else {
            structSym->addField(field.name, *field.type);
        }
    }

    if (!_global_scope->defineStruct(structSym)) {
        errorDuplicateDefinition(decl.name, SymbolKind::Struct, {});
    }
}

void Binder::collectFunction(const FunctionDeclaration &decl) const {
    collectFunctionWithPrefix(decl, "");
}

void Binder::collectFunctionWithPrefix(const FunctionDeclaration &decl, const std::string &prefix) const {
    // "main" is always in global namespace
    const std::string qualifiedName = (decl.name == "main" || prefix.empty())
                                          ? decl.name
                                          : prefix + "::" + decl.name;
    const auto funcSym = std::make_shared<FunctionSymbol>(qualifiedName, *decl.returnType);

    for (const auto &param: decl.parameters) {
        funcSym->addParameter(param.name, *param.type);
    }

    if (!_global_scope->defineFunction(funcSym)) {
        errorDuplicateDefinition(qualifiedName, SymbolKind::Function, {});
    }
}

void Binder::collectStructWithPrefix(const StructDeclaration &decl, const std::string &prefix) const {
    const std::string qualifiedName = prefix.empty() ? decl.name : prefix + "::" + decl.name;
    const auto structSym = std::make_shared<StructSymbol>(qualifiedName);

    for (const auto &genParam: decl.genericParams.params) {
        structSym->addGenericParam(genParam.name);
    }

    for (const auto &field: decl.fields) {
        if (structSym->hasField(field.name)) {
            errorDuplicateDefinition(field.name, SymbolKind::Field, {});
        } else {
            structSym->addField(field.name, *field.type);
        }
    }

    if (!_global_scope->defineStruct(structSym)) {
        errorDuplicateDefinition(qualifiedName, SymbolKind::Struct, {});
    }
}

void Binder::collectNamespace(const NamespaceDeclaration &ns, const std::string &prefix) const {
    const std::string qualifiedPrefix = prefix.empty() ? ns.name : prefix + "::" + ns.name;

    // Collect structs in namespace
    for (const auto &struc: ns.structs) {
        collectStructWithPrefix(*struc, qualifiedPrefix);
    }

    // Collect functions in namespace
    for (const auto &func: ns.functions) {
        collectFunctionWithPrefix(*func, qualifiedPrefix);
    }

    // Recursively collect nested namespaces
    for (const auto &nestedNs: ns.namespaces) {
        collectNamespace(*nestedNs, qualifiedPrefix);
    }
}

void Binder::processImports(const Program &program) const {
    for (const auto &import: program.imports) {
        const std::string nsPath = import->namespacePath.toString();

        for (const auto &[name, symbol]: _global_scope->symbols()) {
            if (name.starts_with(nsPath + "::")) {
                if (const std::string shortName = name.substr(nsPath.length() + 2);
                    shortName.find("::") == std::string::npos) {
                    if (!_global_scope->isDefinedLocally(shortName)) {
                        _global_scope->defineAlias(shortName, symbol);
                    }
                }
            }
        }
    }
}


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
    }
}

void Binder::bindExpression(const Expression &expr) {
    if (const auto *id = dynamic_cast<const Identifier *>(&expr)) {
        bindIdentifier(*id);
    } else if (const auto *call = dynamic_cast<const FunctionCall *>(&expr)) {
        bindFunctionCall(*call);
    } else if (const auto *access = dynamic_cast<const FieldAccess *>(&expr)) {
        bindFieldAccess(*access);
    } else if (const auto *binary = dynamic_cast<const BinaryExpression *>(&expr)) {
        bindBinaryExpression(*binary);
    } else if (const auto *unary = dynamic_cast<const UnaryExpression *>(&expr)) {
        bindUnaryExpression(*unary);
    } else if (const auto *varDecl = dynamic_cast<const VariableDeclaration *>(&expr)) {
        bindVariableDeclaration(*varDecl);
    } else if (const auto *varInit = dynamic_cast<const VariableInit *>(&expr)) {
        bindVariableInit(*varInit);
    } else if (const auto *assign = dynamic_cast<const Assignment *>(&expr)) {
        bindAssignment(*assign);
    } else if (const auto *braceInit = dynamic_cast<const BraceInitializer *>(&expr)) {
        bindBraceInitializer(*braceInit);
    }
}

void Binder::bindIdentifier(const Identifier &id) const {
    if (const auto sym = _current_scope->lookupVariable(id.name); sym) {
        _current_scope->markUsed(id.name);
        return;
    }

    if (const auto funcSym = _current_scope->lookupFunction(id.name); !funcSym) {
        errorUndefinedVariable(id.name, {});
    }
}

void Binder::bindFunctionCall(const FunctionCall &call) {
    if (const auto funcSym = _global_scope->lookupFunction(call.name); funcSym) {
        if (!funcSym->isVariadic && call.arguments.size() != funcSym->arity()) {
            errorWrongArgumentCount(call.name, funcSym->arity(), call.arguments.size(), {});
        } else if (funcSym->isVariadic && call.arguments.size() < funcSym->arity()) {
            errorWrongArgumentCount(call.name, funcSym->arity(), call.arguments.size(), {});
        }
    } else {
        errorUndefinedFunction(call.name, {});
    }

    for (const auto &arg: call.arguments) {
        bindExpression(*arg);
    }
}

void Binder::bindFieldAccess(const FieldAccess &access) {
    bindExpression(*access.object);

    // TODO: Type checking would validate the field exists on the struct type
    // For now, we just bind the expression - field validation happens in type checking
}

void Binder::bindBinaryExpression(const BinaryExpression &expr) {
    bindExpression(*expr.left);
    bindExpression(*expr.right);
}

void Binder::bindUnaryExpression(const UnaryExpression &expr) {
    bindExpression(*expr.operand);
}

void Binder::bindVariableDeclaration(const VariableDeclaration &decl) {
    if (!isTypeDefined(decl.type)) {
        if (decl.type.kind == TypeKind::STRUCT) {
            errorUndefinedStruct(decl.type.structName, {});
        }
    }

    if (!_current_scope->defineVariable(decl.name, decl.type, decl.isMutable)) {
        errorDuplicateDefinition(decl.name, SymbolKind::Variable, {});
    }
}

void Binder::bindVariableInit(const VariableInit &init) {
    if (!isTypeDefined(init.type)) {
        if (init.type.kind == TypeKind::STRUCT) {
            errorUndefinedStruct(init.type.structName, {});
        }
    }

    if (init.value) {
        if (const auto *braceInit = dynamic_cast<const BraceInitializer *>(init.value.get())) {
            bindBraceInitializer(*braceInit, &init.type);
        } else {
            bindExpression(*init.value);
        }
    }

    const auto symbol = std::make_shared<Symbol>(SymbolKind::Variable, init.name, init.type, SourceLocation{},
                                                 init.isMutable);
    symbol->isInitialized = true;
    if (!_current_scope->define(symbol)) {
        errorDuplicateDefinition(init.name, SymbolKind::Variable, {});
    }
}

void Binder::bindAssignment(const Assignment &assign) {
    if (const auto symbol = _current_scope->lookupVariable(assign.name); symbol) {
        if (!symbol->isMutable) {
            errorImmutableVariable(assign.name, {});
        }

        symbol->isInitialized = true;
        _current_scope->markUsed(assign.name);
    } else {
        errorUndefinedVariable(assign.name, {});
    }

    if (assign.value) {
        bindExpression(*assign.value);
    }
}

void Binder::bindBraceInitializer(const BraceInitializer &init, const Type *expectedType) {
    if (expectedType && expectedType->kind == TypeKind::STRUCT) {
        if (const auto structSym = _global_scope->lookupStruct(expectedType->structName)) {
            for (const auto &elem: init.elements) {
                if (elem.isDesignated()) {
                    if (!structSym->hasField(elem.fieldName)) {
                        errorUndefinedField(expectedType->structName, elem.fieldName, {});
                    }
                }
                if (elem.value) {
                    bindExpression(*elem.value);
                }
            }
            return;
        }
    }

    for (const auto &elem: init.elements) {
        if (elem.value) {
            bindExpression(*elem.value);
        }
    }
}

bool Binder::resolveType(const Type &type) {
    return isTypeDefined(type);
}

bool Binder::isTypeDefined(const Type &type) {
    switch (type.kind) {
        case TypeKind::INTEGER:
        case TypeKind::STRING:
        case TypeKind::VOID:
        case TypeKind::F16:
        case TypeKind::F32:
        case TypeKind::F64:
        case TypeKind::F128:
        case TypeKind::AUTO:
            return true;

        case TypeKind::STRUCT:
            return _global_scope->lookupStruct(type.structName) != nullptr;

        case TypeKind::ARRAY:
        case TypeKind::POINTER:
            if (type.elementType) {
                return isTypeDefined(*type.elementType);
            }
            return false;

        default:
            return false;
    }
}

void Binder::errorImmutableVariable(const std::string &name, const SourceLocation loc) const {
    _diagnostics.error(DiagnosticCode::IMMUTABLE_MODIFICATION,
                       "tried to modify a immutable variable '" + name + "'", loc);
}

void Binder::errorUndefinedVariable(const std::string &name, const SourceLocation loc) const {
    _diagnostics.error(DiagnosticCode::UNDEFINED_VARIABLE,
                       "undefined variable '" + name + "'", loc);
}

void Binder::errorUndefinedFunction(const std::string &name, const SourceLocation loc) const {
    _diagnostics.error(DiagnosticCode::UNDEFINED_FUNCTION,
                       "undefined function '" + name + "'", loc);
}

void Binder::errorUndefinedStruct(const std::string &name, const SourceLocation loc) const {
    _diagnostics.error(DiagnosticCode::UNDEFINED_STRUCT,
                       "undefined struct '" + name + "'", loc);
}

void Binder::errorUndefinedField(const std::string &structName, const std::string &fieldName,
                                 const SourceLocation loc) const {
    _diagnostics.error(DiagnosticCode::UNDEFINED_FIELD,
                       "struct '" + structName + "' has no field named '" + fieldName + "'", loc);
}

void Binder::errorDuplicateDefinition(const std::string &name, const SymbolKind kind, const SourceLocation loc) const {
    _diagnostics.error(DiagnosticCode::DUPLICATE_DEFINITION,
                       Symbol::kindToString(kind) + " '" + name + "' is already defined", loc);
}

void Binder::errorWrongArgumentCount(const std::string &funcName, const size_t expected, const size_t got,
                                     const SourceLocation loc) const {
    _diagnostics.error(DiagnosticCode::TYPE_MISMATCH,
                       "function '" + funcName + "' expects " + std::to_string(expected) +
                       " arguments but got " + std::to_string(got), loc);
}
