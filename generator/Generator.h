//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_GENERATOR_H
#define DJINN_GENERATOR_H

#include <memory>
#include <unordered_map>
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "../parser/AST.h"
#include "../diagnostics/Diagnostic.h"
#include "GeneratorScope.h"

class Generator {
public:
    Generator();

    void generate(const Program &program);

    void optimize() const;

    std::string print() const;

    llvm::Module &getModule() { return *module; }

private:
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<> > builder;

    std::unordered_map<std::string, llvm::Function *> functions;
    std::shared_ptr<GeneratorScope> currentScope;

    void push_scope();

    void pop_scope();

    void declare_extern_functions();

    void generate_default_main();

    llvm::Function *generate_function(const std::string &name, const Type &returnType,
                                      const std::vector<std::pair<Type, std::string> > &parameters);

    void generate_struct(const StructDeclaration &struct_declaration);

    void generate_function(const FunctionDeclaration &func);

    llvm::Type *generate_type(const Type &type);

    void generate_statement(const Statement &stmt);

    llvm::Value *generate_expression(const Expression &expr);

    llvm::Value *generate_integer_literal(const IntegerLiteral &expr);

    llvm::Value *generate_string_literal(const StringLiteral &expr);

    llvm::Value *generate_binary_expression(const BinaryExpression &expr);

    llvm::Value *generate_unary_expression(const UnaryExpression &expr);

    llvm::Value *generate_function_call(const FunctionCall &expr);

    llvm::Value *generate_identifier(const Identifier &expr);

    llvm::Value *generate_variable_declaration(const VariableDeclaration &expr);

    llvm::Value *generate_field_access(const FieldAccess &expr);

    llvm::Value *generate_variable_init(const VariableInit &expr);

    llvm::Value *generate_assignment(const Assignment &expr);

    llvm::Value *generate_brace_initializer(const BraceInitializer &expr);

    llvm::Value *generate_brace_init_for_struct(const BraceInitializer &braceInit, llvm::StructType *structType,
                                                const std::string &structName);

    llvm::Value *cast_value(llvm::Value *value, llvm::Type *targetType) const;

    llvm::Function *currentFunction = nullptr;
};

#endif //DJINN_GENERATOR_H