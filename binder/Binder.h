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
#include "scope/ControlFlowContext.h"
#include "ownership/OwnershipTracker.h"
#include "../parser/ast/Declaration.h"
#include "../parser/ast/Statement.h"
#include "../parser/ast/Expression.h"
#include "../diagnostics/Diagnostic.h"
#include <cassert>


#define BINDER_ERROR(code, msg, token, location) do { \
    _diagnostics.emitAndPrint(Diagnostic(Severity::Error, code, msg, location)); \
    throw CompileError(code, msg); \
} while (false)

#define BINDER_WARNING(code, msg, location) do { \
    if (!_bindingStdLib) { \
        _diagnostics.emitAndPrint(Diagnostic(Severity::Warning, code, msg, location)); \
    } \
} while (false)

struct Program;
struct FunctionDeclaration;
struct ExternFunctionDeclaration;
struct StructDeclaration;
struct Block;
struct Statement;
struct Expression;

struct BindingResult
{
    bool success = true;
    std::shared_ptr<ScopedSymbolTable> globalScope;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool hasErrors() const { return !success; }
};

namespace djinn
{
    class BinderExpressionVisitor;
    class BinderStatementVisitor;
    class DeclarationCollectorVisitor;
    class ProgramBinderVisitor;
    class ImportProcessorVisitor;
}

namespace djinn::binder
{
    class ScopeGuard;
    class CallHandler;
    class MethodCallHandler;
    class IntrinsicCallHandler;
    class EnumConstructionHandler;
    class ConstructorCallHandler;
    class RegularFunctionCallHandler;
}

class Binder
{
    friend class djinn::BinderExpressionVisitor;
    friend class djinn::BinderStatementVisitor;
    friend class djinn::DeclarationCollectorVisitor;
    friend class djinn::ProgramBinderVisitor;
    friend class djinn::ImportProcessorVisitor;
    friend class djinn::binder::ScopeGuard;
    friend class djinn::binder::CallHandler;
    friend class djinn::binder::MethodCallHandler;
    friend class djinn::binder::IntrinsicCallHandler;
    friend class djinn::binder::EnumConstructionHandler;
    friend class djinn::binder::ConstructorCallHandler;
    friend class djinn::binder::RegularFunctionCallHandler;

public:
    explicit Binder(DiagnosticEngine& diagnostics);

    BindingResult bind(const Program& program);

    BindingResult bindAll(const std::vector<std::shared_ptr<Program>>& programs);

    void defineMacroLocal(const std::string& name)
    {
        _current_scope->defineVariable(name, Type::auto_type(), false);
    }

private:
    DiagnosticEngine& _diagnostics;
    std::shared_ptr<ScopedSymbolTable> _current_scope;
    std::shared_ptr<ScopedSymbolTable> _global_scope;

    std::string currentFunction_;
    std::string currentStructName_;
    bool _bindingStdLib = false;
    djinn::binder::ControlFlowContext _controlFlow;
    djinn::ownership::OwnershipTracker _ownership;

    template <typename T>
    std::string type_to_string(const T& value) const
    {
        if constexpr (std::is_same_v<T, Symbol*>)
            return value->name;
        else if constexpr (std::is_same_v<T, Symbol>)
            return value.name;
        else
            return typeid(T).name();
    }

    void pushScope();

    void popScope();

    void collectDeclarations(const Program& program) const;

    void collectExternFunction(const ExternFunctionDeclaration& decl, const std::string& prefix = "") const;

    void collectStruct(const StructDeclaration& decl, const std::string& prefix = "") const;

    void collectInterface(const InterfaceDeclaration& decl) const;

    void collectInterfaceWithPrefix(const InterfaceDeclaration& decl, const std::string& prefix) const;

    void collectEnum(const EnumDeclaration& decl) const;

    void collectEnumWithPrefix(const EnumDeclaration& decl, const std::string& prefix) const;

    void collectFunction(FunctionDeclaration& decl) const;

    void collectFunctionWithPrefix(FunctionDeclaration& decl, const std::string& prefix) const;


    void collectImpl(const ImplDeclaration& decl, const std::string& prefix) const;

    void collectNamespace(const NamespaceDeclaration& ns, const std::string& prefix) const;

    void processImports(const Program& program) const;

    void bindProgram(const Program& program);

    void bindFunction(const FunctionDeclaration& func, const std::string& prefix = "");

    void bindMethod(StructMethodDeclaration& method, const StructDeclaration& struc);

    void bindBlock(const Block& block);

    void bindNamespace(const NamespaceDeclaration& ns, const std::string& prefix);

    void bindStatement(const Statement& stmt);

    void bindIfStatement(const IfStatement& stmt);

    void bindForStatement(const ForStatement& stmt);

    void bindRangeForStatement(const RangeForStatement& stmt);

    void bindWhileStatement(const WhileStatement& stmt);

    void bindDoWhileStatement(const DoWhileStatement& stmt);

    void bindSwitchStatement(const SwitchStatement& stmt);

    std::shared_ptr<Symbol> bindExpression(const Expression& expr);

    std::shared_ptr<Symbol> bindIndexAccess(const IndexAccess& expr);

    std::shared_ptr<Symbol> bindIdentifier(const Identifier& id);

    std::shared_ptr<Symbol> bindFunctionCall(const FunctionCall& call);

    std::shared_ptr<Symbol> bindFieldAccess(const FieldAccess& access);

    std::shared_ptr<Symbol> bindFieldAssignment(const FieldAssignment& assign);

    std::shared_ptr<Symbol> bindBinaryExpression(const BinaryExpression& expr);

    std::shared_ptr<Symbol> bindUnaryExpression(const UnaryExpression& expr);

    std::shared_ptr<Symbol> bindPostfixExpression(const PostfixExpression& expr);

    std::shared_ptr<Symbol> bindVariableDeclaration(const VariableDeclaration& decl);

    std::shared_ptr<Symbol> bindVariableInit(const VariableInit& init);

    std::shared_ptr<Symbol> bindAssignment(const Assignment& assign);

    std::shared_ptr<Symbol> bindBraceInitializer(const BraceInitializer& init, const Type* expectedType = nullptr);

    std::shared_ptr<Symbol> bindNewExpression(const NewExpression& expr);

    std::unique_ptr<Type> resolveType(const Type& type) const;

    static bool is_generic_type(const Type& type, const StructDeclaration& struc);

    bool isTypeDefined(const Type& type) const;

    std::optional<Type> inferExpressionType(const Expression& expr) const;

    // Type compatibility checking
    void checkTypeCompatibility(const Type& expected, const Expression& expr, SourceLocation loc);

    // Validation helpers for visitor
    void validateBreakStatement(const BreakStatement& stmt);

    void validateContinueStatement(const ContinueStatement& stmt);

    // Type validation helpers for visitors
    void validateExternFunctionTypes(const ExternFunctionDeclaration& decl);

    void validateStructFieldTypes(const StructDeclaration& decl);

    // Ownership and borrow checking
    void trackVariableDefinition(const std::string& name, const Type& type, SourceLocation loc);

    void checkVariableUse(const std::string& name, SourceLocation loc);

    void checkVariableMove(const std::string& name, SourceLocation loc);

    void checkVariableBorrow(const std::string& name, djinn::ownership::BorrowKind kind,
                             const std::string& borrower, SourceLocation loc);

    void checkVariableAssignment(const std::string& name, SourceLocation loc);

    void performMove(const std::string& name, SourceLocation loc);

    void performBorrow(const std::string& name, djinn::ownership::BorrowKind kind,
                       const std::string& borrower, SourceLocation loc);

    void reinitializeVariable(const std::string& name);
};

#endif //DJINN_BINDER_H