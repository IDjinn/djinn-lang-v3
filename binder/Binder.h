//
// Created by Claude on 04/01/2026.
//

#ifndef DJINN_BINDER_H
#define DJINN_BINDER_H

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "SymbolTable.h"
#include "../parser/ast/Declaration.h"
#include "../parser/ast/Statement.h"
#include "../parser/ast/Expression.h"
#include "../diagnostics/Diagnostic.h"

#define BINDER_ERROR(code, msg, token, location) do { \
    _diagnostics.emitAndPrint(Diagnostic(Severity::Error, code, msg, location)); \
} while (false)

#define BINDER_WARNING(code, msg, location) do { \
    _diagnostics.emitAndPrint(Diagnostic(Severity::Warning, code, msg, location)); \
} while (false)

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

    BindingResult bindAll(const std::vector<std::shared_ptr<Program> > &programs);

private:
    DiagnosticEngine &_diagnostics;
    std::shared_ptr<ScopedSymbolTable> _current_scope;
    std::shared_ptr<ScopedSymbolTable> _global_scope;

    std::string currentFunction_;
    int loopDepth_ = 0;
    int switchDepth_ = 0;

    template<typename T>
    constexpr std::string type_to_string(const T &value) const {
        if constexpr (std::is_same_v<T, Symbol *>)
            return value->name;
        else if constexpr (std::is_same_v<T, Symbol>)
            return value.name;
        else
            return typeid(T).name();
    }

    void pushScope();

    void popScope();

    void collectDeclarations(const Program &program);

    void collectExternFunction(const ExternFunctionDeclaration &decl) const;

    void collectStruct(const StructDeclaration &decl) const;

    void collectInterface(const InterfaceDeclaration &decl) const;

    void collectInterfaceWithPrefix(const InterfaceDeclaration &decl, const std::string &prefix) const;

    void collectEnum(const EnumDeclaration &decl) const;

    void collectEnumWithPrefix(const EnumDeclaration &decl, const std::string &prefix) const;

    void collectFunction(const FunctionDeclaration &decl) const;

    void collectFunctionWithPrefix(const FunctionDeclaration &decl, const std::string &prefix) const;

    bool isTypeDefined(Type *type) const;

    void collectStructWithPrefix(const StructDeclaration &decl, const std::string &prefix) const;

    void collectNamespace(const NamespaceDeclaration &ns, const std::string &prefix) const;

    void processImports(const Program &program) const;

    void bindProgram(const Program &program);

    void bindFunction(const FunctionDeclaration &func);

    void bindMethod(StructMethodDeclaration &method, const StructDeclaration &struc);

    void bindBlock(const Block &block);

    void bindNamespace(const NamespaceDeclaration &ns, const std::string &prefix);

    void bindStatement(const Statement &stmt);

    void bindIfStatement(const IfStatement &stmt);

    void bindForStatement(const ForStatement &stmt);

    void bindWhileStatement(const WhileStatement &stmt);

    void bindDoWhileStatement(const DoWhileStatement &stmt);

    void bindSwitchStatement(const SwitchStatement &stmt);

    void bindExpression(const Expression &expr);

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

    std::unique_ptr<Type> resolveType(const Type &type) const;

    static bool is_generic_type(const Type &type, const StructDeclaration &struc);

    bool isTypeDefined(Type &type) const;

    bool isTypeDefined(const Type &type) const;

    std::optional<Type> inferExpressionType(const Expression &expr) const;

    // Type compatibility checking
    void checkTypeCompatibility(const Type &expected, const Expression &expr, SourceLocation loc);
};

#endif //DJINN_BINDER_H