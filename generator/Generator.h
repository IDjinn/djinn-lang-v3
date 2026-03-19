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
throw CompileError(code, msg); \
assert(false); \
} while (false)

#define GENERATOR_WARN(code, msg, location) do { \
_diagnostics.emitAndPrint(Diagnostic(Severity::Warning, code, msg, location)); \
} while (false)

namespace djinn
{
    class GeneratorExpressionVisitor;
    class GeneratorStatementVisitor;
}

class Generator
{
    friend class djinn::GeneratorExpressionVisitor;
    friend class djinn::GeneratorStatementVisitor;

public:
    Generator(DiagnosticEngine& diagnostics, const std::shared_ptr<ScopedSymbolTable>& symbols);

    void run_passes(bool optimize) const;

    std::string print() const;

    void generate();

    bool linkModules(const std::vector<std::filesystem::path>& allPaths) const;

private:
    DiagnosticEngine& _diagnostics;
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;

    std::unordered_map<std::string, llvm::Function*> functions;
    std::vector<llvm::Function*> externFunctions;
    std::vector<llvm::StructType*> declaredTypes;
    std::shared_ptr<GeneratorScope> currentScope;
    const std::shared_ptr<ScopedSymbolTable>& symbols;

    void push_scope();

    void pop_scope();

    void generate_default_main();

    void forward_declare_struct(const StructSymbol& struct_symbol);

    void resolve_struct_body(const StructSymbol& struct_symbol);

    void forward_declare_struct_methods(const StructSymbol& struct_symbol);

    void generate_struct_methods(const StructSymbol& struct_symbol);

    void forward_declare_method(const StructSymbol& struc, const MethodSymbol& method);

    void generate_method(const StructSymbol& struc, const MethodSymbol& method);

    bool is_primitive_impl(const StructSymbol& struc) const;

    void generate_primitive_impl_method(const StructSymbol& struc, const MethodSymbol& method);

    static std::string get_primitive_type_name(llvm::Type* type);

    void generate_property(const StructSymbol& struc, const PropertySymbol& prop);

    void forward_declare_function(const FunctionSymbol& func);

    void generate_function_body(const FunctionSymbol& func);

    void generate_extern_function(const ExternFunctionSymbol& func);

    void generate_enum(const EnumSymbol& enum_symbol);

    void resolve_enum_body(const EnumSymbol& enum_symbol);

    llvm::Value* generate_enum_construction(const EnumDef& enumDef, const EnumVariantDef& variant,
                                            const std::vector<std::unique_ptr<Expression>>& args);

    llvm::Value* extract_enum_tag(llvm::Value* enumValue, const EnumDef& enumDef);

    llvm::Value* extract_enum_payload(llvm::Value* enumValue, const EnumDef& enumDef, size_t variantIdx);

    llvm::Value* generate_switch_expression(const SwitchExpression& expr);

    void emit_used_declarations();

    llvm::Type* generate_type(const Type& type);

    llvm::StructType* monomorphize_struct(const std::string& baseName, const std::vector<Type>& typeArgs);

    llvm::StructType* monomorphize_enum(const std::string& baseName, const std::vector<Type>& typeArgs);

    void forward_declare_monomorphized_method(const MethodSymbol& method,
                                              llvm::StructType* monomorphizedType,
                                              const GenericContext& ctx,
                                              const std::string& mangledStructName);

    void monomorphize_method(const MethodSymbol& method,
                             llvm::StructType* monomorphizedType,
                             const GenericContext& ctx,
                             const std::string& mangledStructName);

    void monomorphize_property(const PropertySymbol& prop,
                               llvm::StructType* monomorphizedType,
                               const GenericContext& ctx,
                               const std::string& mangledStructName);

    void validate_generic_constraints(const GenericParams& params, const GenericArgs& args,
                                      const std::string& contextName);

    bool type_satisfies_constraint(const Type& type, const std::string& interfaceName);

    llvm::Type* generate_type_with_context(const Type& type, const GenericContext* ctx);

    void generate_statement(const Statement& stmt);

    void generate_if_statement(const IfStatement& stmt);

    void generate_for_statement(const ForStatement& stmt);

    void generate_while_statement(const WhileStatement& stmt);

    void generate_do_while_statement(const DoWhileStatement& stmt);

    void generate_switch_statement(const SwitchStatement& stmt);

    void generate_block(const Block& block);

    llvm::Value* generate_expression(const Expression& expr);

    llvm::Value* generate_integer_literal(const IntegerLiteral& expr) const;

    llvm::Value* generate_float_literal(const FloatLiteral& expr) const;

    llvm::Value* generate_string_literal(const StringLiteral& expr);

    llvm::Value* coerce_str_to_ptr(llvm::Value* value);

    llvm::Value* extract_slice_data_ptr(llvm::Value* value);

    llvm::Value* generate_binary_expression(const BinaryExpression& expr);

    llvm::Value* generate_unary_expression(const UnaryExpression& expr);

    llvm::Value* generate_function_call(const FunctionCall& expr);

    llvm::Value* generate_new_expression(const NewExpression& expr);

    llvm::Value* generate_array_literal(const ArrayLiteral& expr);

    llvm::Value* generate_method_call_internal(const FunctionCall& call);

    llvm::Value* generate_intrinsic_call(const FunctionCall& call);

    static bool is_intrinsic(const std::string& name);

    static bool is_slice_struct(llvm::StructType* st);

    llvm::Value* generate_identifier(const Identifier& expr) const;

    llvm::Value* generate_variable_declaration(const VariableDeclaration& expr);

    llvm::Value* generate_field_access(const FieldAccess& expr);

    llvm::Value* generate_field_assignment(const FieldAssignment& expr);

    llvm::Value* generate_index_access(const IndexAccess& expr);

    llvm::Value* generate_index_assignment(const IndexAssignment& expr);

    llvm::Type* resolve_index_element_type(const Expression& objectExpr);

    llvm::Value* generate_variable_init(const VariableInit& expr);

    llvm::Value* generate_assignment(const Assignment& expr);

    llvm::Value* generate_cast_expression(const CastExpression& expr);

    llvm::Value* generate_await_expression(const AwaitExpression& expr);

    llvm::Value* generate_brace_initializer(const BraceInitializer& expr);

    llvm::Value* generate_brace_init_for_struct(const BraceInitializer& braceInit, llvm::StructType* structType,
                                                const std::string& structName);

    llvm::Value* cast_value(llvm::Value* value, llvm::Type* targetType, bool isSigned = true) const;

    const GenericContext* _currentGenericCtx = nullptr;

    llvm::Function* currentFunction = nullptr;

    std::vector<llvm::BasicBlock*> breakTargets;
    std::vector<llvm::BasicBlock*> continueTargets;

    // Generation verification (generates default main if missing)
    void verify_all_symbols_generated();

    // Helper methods for visitor pattern
    void generate_return_statement(const ReturnStatement& stmt);

    void generate_break_statement();

    void generate_continue_statement();

    void generate_yield_statement();

    // RAII cleanup
    llvm::Function* find_destroy_method(llvm::AllocaInst* alloca);
    void emit_scope_cleanup();
    void emit_all_scope_cleanup();

    // Async/coroutine state
    bool inAsyncFunction = false;
    llvm::Value* asyncCoroId = nullptr;
    llvm::Value* asyncCoroHandle = nullptr;
    llvm::Value* asyncPromisePtr = nullptr;
    llvm::BasicBlock* asyncFinalSuspendBB = nullptr;
    llvm::BasicBlock* asyncCleanupBB = nullptr;
    llvm::BasicBlock* asyncSuspendBB = nullptr;
    llvm::Type* asyncReturnType = nullptr; // original return type before coroutine transform

    void generate_async_function_body(const FunctionSymbol& func);
    void generate_async_method_body(const StructSymbol& struc, const MethodSymbol& method,
                                    llvm::Function* llvmFunc, StructDef* def);
    llvm::Value* generate_await_loop(llvm::Value* handle, llvm::Type* resultType);
    llvm::Value* generate_await_in_async(llvm::Value* childHandle, llvm::Type* resultType);
    void ensure_malloc_free_declared();
    bool hasAsyncFunctions = false;

    // Async runtime support
    void generate_coro_wrappers();
    void generate_runtime_declarations();
    llvm::Value* generate_task_intrinsic_method(const FunctionCall& call, llvm::AllocaInst* receiverAlloca,
                                                const StructDef* def);
    void generate_spawn_statement(const SpawnStatement& stmt);

    // [intrinsic] struct method support
    llvm::Value* generate_intrinsic_method(const FunctionCall& call, const StructDef* def,
                                           const std::string& methodName);
    llvm::Value* generate_coro_intrinsic(const FunctionCall& call, const std::string& method);

    // Temporary state for propagating pointee type info from field access to variable init
    llvm::Type* _lastFieldAccessPointeeType = nullptr;
    std::string _lastFieldAccessStructName;

    size_t generatedFunctions = 0;
    size_t generatedExternFunctions = 0;
    size_t generatedStructs = 0;
    size_t generatedMethods = 0;

    // TypeInfo constants for object boxing (variadics)
    std::unordered_map<std::string, llvm::GlobalVariable*> typeInfoConstants;
    llvm::GlobalVariable* get_or_create_typeinfo(const std::string& typeName, llvm::Type* llvmType);
    static int32_t compute_type_id(const std::string& typeName);
    static uint8_t compute_type_kind(llvm::Type* type, const std::string& typeName);
    llvm::Value* box_value(llvm::Value* value, const std::string& typeName);
    std::string get_type_name_for_value(llvm::Value* value);
};

#endif //DJINN_GENERATOR_H