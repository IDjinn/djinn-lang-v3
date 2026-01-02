//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_CODEGEN_H
#define DJINN_CODEGEN_H

#include <memory>
#include <unordered_map>
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Passes/PassBuilder.h"
#include "../parser/AST.h"
#include "../diagnostics/Diagnostic.h"

class CodeGen {
public:
    CodeGen();

    void generate(const Program& program);
    void optimize();

    std::string print() const;
    llvm::Module& getModule() { return *module; }

private:
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;

    std::unordered_map<std::string, llvm::Function*> functions;
    std::unordered_map<std::string, llvm::AllocaInst*> namedValues;
    std::unordered_map<std::string, std::string> variableStructTypes; // varName -> structTypeName
    std::unordered_map<std::string, llvm::StructType *> structTypes;
    std::unordered_map<std::string, std::unordered_map<std::string, unsigned> > structFieldIndices;

    void declare_extern_functions();
    void generate_default_main();

    void generate_struct(const StructDeclaration &structDecl);

    void generate_function(const FunctionDeclaration& func);

    llvm::Type *generate_type(Type &type);

    void generate_statement(const Statement& stmt);
    llvm::Value* generate_expression(const Expression& expr);

    llvm::Value *generate_brace_init_for_struct(const BraceInitializer &braceInit, llvm::StructType *structType,
                                                const std::string &structName);

    llvm::Value* cast_value(llvm::Value *value, llvm::Type *targetType);

    llvm::Function* currentFunction = nullptr;
};

#endif //DJINN_CODEGEN_H
