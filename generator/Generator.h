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
#include "../parser/AST.h"
#include "../diagnostics/Diagnostic.h"
#include "GeneratorScope.h"

class Generator {
public:
    explicit Generator(const std::string &module_name);

    void generate(const Program &program);

    void optimize() const;

    std::string print() const;

    llvm::Module &getModule() const { return *module; }

private:
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<> > builder;

    std::unordered_map<std::string, llvm::Function *> functions;
    std::shared_ptr<GeneratorScope> currentScope;

    void push_scope();

    void pop_scope();

    void generate_default_main();

    llvm::Function *generate_function(const std::string &name, const Type &returnType,
                                      const std::vector<std::pair<Type, std::string> > &parameters);

    void generate_struct(const StructDeclaration &struct_declaration, const std::string &prefix = "");

    // Multi-pass struct generation (like C#)
    void forward_declare_struct(const StructDeclaration &struct_declaration, const std::string &prefix = "");

    void resolve_struct_body(const StructDeclaration &struct_declaration, const std::string &prefix = "");

    void generate_struct_methods(const StructDeclaration &struct_declaration, const std::string &prefix = "");

    // Namespace multi-pass helpers
    void forward_declare_namespace_structs(const NamespaceDeclaration &ns, const std::string &prefix);

    void resolve_namespace_struct_bodies(const NamespaceDeclaration &ns, const std::string &prefix);

    void generate_namespace_struct_methods(const NamespaceDeclaration &ns, const std::string &prefix);

    void generate_method(const StructMethodDeclaration &method, const StructDeclaration &struc,
                         llvm::StructType *structType);

    // Property generation (C# style getters/setters)
    void generate_property(const StructProperty &prop, const StructDeclaration &struc,
                           llvm::StructType *structType, const std::string &qualifiedName);

    void generate_function(const FunctionDeclaration &func, const std::string &prefix = "");

    void generate_extern_function(const ExternFunctionDeclaration &decl);

    void generate_namespace(const NamespaceDeclaration &ns, const std::string &prefix = "");

    void generate_namespace_structs(const NamespaceDeclaration &ns, const std::string &prefix);

    void generate_namespace_functions(const NamespaceDeclaration &ns, const std::string &prefix);

    llvm::Type *generate_type(const Type &type);

    // Monomorphization for generics
    llvm::StructType *monomorphize_struct(const std::string &baseName, const std::vector<Type> &typeArgs);

    void monomorphize_method(const StructMethodDeclaration &method,
                             const StructDef &genericDef,
                             llvm::StructType *monomorphizedType,
                             const GenericContext &ctx,
                             const std::string &mangledStructName);

    void monomorphize_property(const StructProperty &prop,
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

    // Stack for break/continue targets
    std::vector<llvm::BasicBlock *> breakTargets;
    std::vector<llvm::BasicBlock *> continueTargets;
};

#endif //DJINN_GENERATOR_H