//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_GENERATOR_H
#define DJINN_GENERATOR_H

#include <memory>
#include <unordered_map>
#include <vector>
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include <filesystem>
#include "../parser/AST.h"
#include "GeneratorScope.h"
#include "../binder/Symbol.h"
#include "../binder/SymbolTable.h"
#include "../diagnostics/Diagnostic.h"

#define GENERATOR_ERROR(code, msg, location) do { \
    _diagnostics.emitAndPrint(Diagnostic(Severity::Error, code, msg, location)); \
    throw CompileError(code, msg, location); \
} while (false)

#define GENERATOR_WARN(code, msg, location) do { \
    _diagnostics.emitAndPrint(Diagnostic(Severity::Warning, code, msg, location)); \
} while (false)

namespace djinn {
    class GeneratorExpressionVisitor;
    class GeneratorStatementVisitor;
}

class Generator {
    friend class djinn::GeneratorExpressionVisitor;
    friend class djinn::GeneratorStatementVisitor;

public:
    Generator(DiagnosticEngine &diagnostics, const std::shared_ptr<ScopedSymbolTable> &symbols);

    void optimize() const;

    std::string print() const;

    void generate();

    bool linkModules(const std::vector<std::filesystem::path> &allPaths) const;

private:
    DiagnosticEngine &_diagnostics;
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<> > builder;

    std::unordered_map<std::string, llvm::Function *> functions;
    std::vector<llvm::Function *> externFunctions;
    std::vector<llvm::StructType *> declaredTypes;
    std::shared_ptr<GeneratorScope> currentScope;
    const std::shared_ptr<ScopedSymbolTable> &symbols;

    void push_scope();

    void pop_scope();

    void generate_default_main();

    void forward_declare_struct(const StructSymbol &struct_symbol);

    void resolve_struct_body(const StructSymbol &struct_symbol);

    void generate_struct_methods(const StructSymbol &struct_symbol);

    void generate_method(const StructSymbol &struc, const MethodSymbol &method);

    void generate_property(const StructSymbol &struc, const PropertySymbol &prop);

    void forward_declare_function(const FunctionSymbol &func);

    void generate_function_body(const FunctionSymbol &func);

    void generate_extern_function(const ExternFunctionSymbol &func);

    void generate_enum(const EnumSymbol &enum_symbol);

    void resolve_enum_body(const EnumSymbol &enum_symbol);

    llvm::Value *generate_enum_construction(const EnumDef &enumDef, const EnumVariantDef &variant,
                                            const std::vector<std::unique_ptr<Expression> > &args);

    llvm::Value *extract_enum_tag(llvm::Value *enumValue, const EnumDef &enumDef);

    llvm::Value *extract_enum_payload(llvm::Value *enumValue, const EnumDef &enumDef, size_t variantIdx);

    llvm::Value *generate_switch_expression(const SwitchExpression &expr);

    void emit_used_declarations();

    llvm::Type *generate_type(const Type &type);

    llvm::StructType *monomorphize_struct(const std::string &baseName, const std::vector<Type> &typeArgs);

    llvm::StructType *monomorphize_enum(const std::string &baseName, const std::vector<Type> &typeArgs);

    void monomorphize_method(const MethodSymbol &method,
                             llvm::StructType *monomorphizedType,
                             const GenericContext &ctx,
                             const std::string &mangledStructName);

    void monomorphize_property(const PropertySymbol &prop,
                               llvm::StructType *monomorphizedType,
                               const GenericContext &ctx,
                               const std::string &mangledStructName);

    llvm::Type *generate_type_with_context(const Type &type, const GenericContext *ctx);

    void generate_statement(const Statement &stmt);

    void generate_if_statement(const IfStatement &stmt);

    void generate_for_statement(const ForStatement &stmt);

    void generate_while_statement(const WhileStatement &stmt);

    void generate_do_while_statement(const DoWhileStatement &stmt);

    void generate_switch_statement(const SwitchStatement &stmt);

    void generate_block(const Block &block);

    llvm::Value *generate_expression(const Expression &expr);

    llvm::Value *generate_integer_literal(const IntegerLiteral &expr) const;

    llvm::Value *generate_float_literal(const FloatLiteral &expr) const;

    llvm::Value *generate_string_literal(const StringLiteral &expr) const;

    llvm::Value *generate_binary_expression(const BinaryExpression &expr);

    llvm::Value *generate_unary_expression(const UnaryExpression &expr);

    llvm::Value *generate_function_call(const FunctionCall &expr);

    llvm::Value *generate_method_call_internal(const FunctionCall &call);

    llvm::Value *generate_intrinsic_call(const FunctionCall &call);

    static bool is_intrinsic(const std::string &name);

    llvm::Value *generate_identifier(const Identifier &expr) const;

    llvm::Value *generate_variable_declaration(const VariableDeclaration &expr);

    llvm::Value *generate_field_access(const FieldAccess &expr);

    llvm::Value *generate_field_assignment(const FieldAssignment &expr);

    llvm::Value *generate_variable_init(const VariableInit &expr);

    llvm::Value *generate_assignment(const Assignment &expr);

    llvm::Value *generate_brace_initializer(const BraceInitializer &expr);

    llvm::Value *generate_brace_init_for_struct(const BraceInitializer &braceInit, llvm::StructType *structType,
                                                const std::string &structName);

    llvm::Value *cast_value(llvm::Value *value, llvm::Type *targetType) const;

    llvm::Function *currentFunction = nullptr;

    std::vector<llvm::BasicBlock *> breakTargets;
    std::vector<llvm::BasicBlock *> continueTargets;

    // Generation verification (generates default main if missing)
    void verify_all_symbols_generated();

    // Helper methods for visitor pattern
    void generate_return_statement(const ReturnStatement &stmt);

    void generate_break_statement();

    void generate_continue_statement();

    size_t generatedFunctions = 0;
    size_t generatedExternFunctions = 0;
    size_t generatedStructs = 0;
    size_t generatedMethods = 0;
};

#endif //DJINN_GENERATOR_H