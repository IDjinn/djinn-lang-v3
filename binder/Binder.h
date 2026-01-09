//
// Created by Claude on 04/01/2026.
//

#ifndef DJINN_BINDER_H
#define DJINN_BINDER_H

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "SymbolTable.h"
#include "../parser/ast/Declaration.h"
#include "../parser/ast/Statement.h"
#include "../parser/ast/Expression.h"
#include "../diagnostics/Diagnostic.h"

// Forward declarations
struct Program;
struct FunctionDeclaration;
struct ExternFunctionDeclaration;
struct StructDeclaration;
struct Block;
struct Statement;
struct Expression;

struct BindingResult {
    bool success = true;
    std::shared_ptr<ScopedSymbolTable> globalScope;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool hasErrors() const { return !success; }
};

class Binder {
public:
    explicit Binder(DiagnosticEngine &diagnostics);

    BindingResult bind(const Program &program);

private:
    DiagnosticEngine &_diagnostics;
    std::shared_ptr<ScopedSymbolTable> _current_scope;
    std::shared_ptr<ScopedSymbolTable> _global_scope;

    std::string currentFunction_;
    int loopDepth_ = 0;
    int switchDepth_ = 0;

    void pushScope();

    void popScope();

    // Pass 1: Collect all top-level declarations (allows forward references)
    void collectDeclarations(const Program &program);

    void collectExternFunction(const ExternFunctionDeclaration &decl) const;

    void collectStruct(const StructDeclaration &decl) const;

    void collectInterface(const InterfaceDeclaration &decl) const;

    void collectInterfaceWithPrefix(const InterfaceDeclaration &decl, const std::string &prefix) const;

    void collectFunction(const FunctionDeclaration &decl) const;

    void collectFunctionWithPrefix(const FunctionDeclaration &decl, const std::string &prefix) const;

    void collectStructWithPrefix(const StructDeclaration &decl, const std::string &prefix) const;

    void collectNamespace(const NamespaceDeclaration &ns, const std::string &prefix) const;

    void processImports(const Program &program) const;

    // Pass 2: Bind all references within function bodies
    void bindProgram(const Program &program);

    void bindFunction(const FunctionDeclaration &func);

    void bindMethod(const StructMethodDeclaration &method, const StructDeclaration &struc);

    void bindBlock(const Block &block);

    void bindNamespace(const NamespaceDeclaration &ns, const std::string &prefix);

    void bindStatement(const Statement &stmt);

    void bindIfStatement(const IfStatement &stmt);

    void bindForStatement(const ForStatement &stmt);

    void bindWhileStatement(const WhileStatement &stmt);

    void bindDoWhileStatement(const DoWhileStatement &stmt);

    void bindSwitchStatement(const SwitchStatement &stmt);

    void bindExpression(const Expression &expr);

    // Expression binding helpers
    void bindIdentifier(const Identifier &id) const;

    void bindFunctionCall(const FunctionCall &call);

    void bindFieldAccess(const FieldAccess &access);

    void bindFieldAssignment(const FieldAssignment &assign);

    void bindBinaryExpression(const BinaryExpression &expr);

    void bindUnaryExpression(const UnaryExpression &expr);

    void bindVariableDeclaration(const VariableDeclaration &decl);

    void bindVariableInit(const VariableInit &init);

    void bindAssignment(const Assignment &assign);

    void bindBraceInitializer(const BraceInitializer &init, const Type *expectedType = nullptr);

    // Type resolution
    bool resolveType(const Type &type);

    bool is_generic_type(const Type &type, const StructDeclaration &struc);

    bool isTypeDefined(const Type &type);

    // Type inference for expressions
    std::optional<Type> inferExpressionType(const Expression &expr) const;

    // Type compatibility checking
    void checkTypeCompatibility(const Type &expected, const Expression &expr, SourceLocation loc);

    // Error reporting helpers
    void errorImmutableVariable(const std::string &name, SourceLocation loc) const;

    void errorUndefinedVariable(const std::string &name, SourceLocation loc) const;

    void errorUndefinedFunction(const std::string &name, SourceLocation loc) const;

    void errorUndefinedStruct(const std::string &name, SourceLocation loc) const;

    void errorUndefinedField(const std::string &structName, const std::string &fieldName, SourceLocation loc) const;

    void errorDuplicateDefinition(const std::string &name, SymbolKind kind, SourceLocation loc) const;

    void errorWrongArgumentCount(const std::string &funcName, size_t expected, size_t got, SourceLocation loc) const;
};

#endif //DJINN_BINDER_H