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
#include "../evaluator/ConstEvaluator.h"


#define GENERATOR_ERROR(code, msg, location) do { \
_diagnostics.emitAndPrint(Diagnostic(Severity::Error, code, msg, location)); \
throw CompileError(code, msg, location); \
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

// One operand of a trapping operation: the value plus, when the operand is a
// named local variable, its storage slot (identity for runtime assignment
// history) and display name.
struct TrapOperand
{
    llvm::Value* value = nullptr;
    llvm::Value* slot = nullptr;
    std::string name;
};

// Level of runtime error instrumentation baked into the generated module.
// Full (debug): rich trap descriptors (source snippet, operand values, variable
// history) and shadow-stack frames for stack traces. Minimal (release): traps
// and uncaught exceptions keep message + file:line + operand values only.
enum class RuntimeDiagnostics
{
    Minimal,
    Full
};

// Layout of error values: { tag: i32, message: ptr, type_name: ptr }
llvm::StructType* djinn_error_value_type(llvm::LLVMContext& context, llvm::IRBuilder<>& builder);

class Generator
{
    friend class djinn::GeneratorExpressionVisitor;
    friend class djinn::GeneratorStatementVisitor;

public:
    Generator(DiagnosticEngine& diagnostics, const std::shared_ptr<ScopedSymbolTable>& symbols);

    void run_passes(bool skipCoroPasses = false) const;

    std::string print() const;

    // Releases module + context ownership for in-process JIT execution.
    std::pair<std::unique_ptr<llvm::Module>, std::unique_ptr<llvm::LLVMContext>> takeModule();

    bool verify() const;

    void generate();

    bool linkModules(const std::vector<std::filesystem::path>& allPaths) const;
    bool linkBitcode(const std::string& bitcodeData) const;

    [[nodiscard]] llvm::Module& getModule() const { return *module; }

    bool libraryMode = false;
    bool stdDeclOnly = false;
    std::string moduleName;
    std::string reflectionMode = "none";
    RuntimeDiagnostics runtimeDiagnostics = RuntimeDiagnostics::Full;

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
    ConstEvaluator constEvaluator;

    void push_scope();

    void pop_scope();

    void register_intrinsic_constants();

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

    void generate_range_for_statement(const RangeForStatement& stmt);

    void generate_while_statement(const WhileStatement& stmt);

    void generate_do_while_statement(const DoWhileStatement& stmt);

    void generate_switch_statement(const SwitchStatement& stmt);

    void generate_block(const Block& block);

    llvm::Value* generate_expression(const Expression& expr);

    llvm::Constant* evaluate_const_initializer(const Expression& expr,
                                               const std::unordered_map<std::string, ConstValue>& knownConstants = {})
    const;

    llvm::Constant* const_value_to_llvm(const ConstValue& val) const;

    llvm::Value* generate_integer_literal(const IntegerLiteral& expr) const;

    llvm::Value* generate_float_literal(const FloatLiteral& expr) const;

    llvm::Value* generate_string_literal(const StringLiteral& expr);

    llvm::Value* coerce_str_to_ptr(llvm::Value* value);

    llvm::Value* extract_slice_data_ptr(llvm::Value* value);

    llvm::Value* generate_binary_expression(const BinaryExpression& expr);

    llvm::Value* generate_unary_expression(const UnaryExpression& expr);

    llvm::Value* generate_postfix_expression(const PostfixExpression& expr);

    llvm::Value* generate_function_call(const FunctionCall& expr);

    llvm::Value* generate_new_expression(const NewExpression& expr);

    llvm::Value* generate_array_literal(const ArrayLiteral& expr);

    llvm::Value* generate_fixed_array(const FixedArrayExpression& expr);

    llvm::Value* generate_method_call_internal(const FunctionCall& call);

    // Resolves (or forward-declares on demand) the LLVM function for a static
    // struct method, mirroring the method-call resolution order.
    llvm::Function* resolve_static_method_function(const std::string& structName, const std::string& methodName);

    // Boxes the arguments after the first `normalParamCount` into an
    // arr<object> value (auto-boxing for Djinn variadic methods).
    llvm::Value* emit_boxed_varargs_array(const std::vector<std::unique_ptr<Expression>>& args,
                                          size_t normalParamCount);

    llvm::Value* generate_intrinsic_call(const FunctionCall& call);

    static bool is_intrinsic(const std::string& name);

    static bool is_slice_struct(llvm::StructType* st);
    bool is_object_type(llvm::Type* type) const;

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

    llvm::Value* generate_is_expression(const IsExpression& expr);

    llvm::Value* generate_await_expression(const AwaitExpression& expr);

    llvm::Value* generate_macro_expansion(const MacroExpansionExpression& expr);

    llvm::Value* generate_brace_initializer(const BraceInitializer& expr);

    llvm::Value* generate_brace_init_for_struct(const BraceInitializer& braceInit, llvm::StructType* structType,
                                                const std::string& structName);

    llvm::Value* cast_value(llvm::Value* value, llvm::Type* targetType, bool isSigned = true) const;

    const GenericContext* _currentGenericCtx = nullptr;

    llvm::Function* currentFunction = nullptr;
    std::string currentStructName; // set during struct method generation

    std::vector<llvm::BasicBlock*> breakTargets;
    std::vector<llvm::BasicBlock*> continueTargets;

    // Generation verification (generates default main if missing)
    void verify_all_symbols_generated();

    // Helper methods for visitor pattern
    void generate_return_statement(const ReturnStatement& stmt);

    void generate_break_statement();

    void generate_continue_statement();

    void generate_yield_statement(const YieldStatement& stmt);

    // Switch arm block yield context ("yield expr;" inside "-> { ... }"):
    // values are stored to the slot and each yield branches to the block,
    // which becomes the arm's contribution to the switch merge
    bool inSwitchArmBlock_ = false;
    llvm::BasicBlock* armYieldBlock_ = nullptr;
    llvm::AllocaInst* armYieldSlot_ = nullptr;
    bool armDidYield_ = false;

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

    void generate_throw_statement(const ThrowStatement& stmt);

    llvm::Value* generate_ternary_expression(const TernaryExpression& expr);

    llvm::Value* generate_try_expression(const TryExpression& expr);

    // Error handling globals (errno-style for throws functions)
    bool currentFunctionThrows = false;
    bool insideTryOperand_ = false;
    llvm::GlobalVariable* errorFlagGlobal = nullptr;
    llvm::GlobalVariable* errorTagGlobal = nullptr;
    llvm::GlobalVariable* errorPayloadGlobal = nullptr;
    llvm::GlobalVariable* errorNameGlobal = nullptr;
    llvm::GlobalVariable* errorOriginFileGlobal = nullptr;
    llvm::GlobalVariable* errorOriginLineGlobal = nullptr;
    llvm::GlobalVariable* errorOriginColumnGlobal = nullptr;
    void ensure_error_globals_declared();
    llvm::Value* get_default_value(llvm::Type* type);
    std::shared_ptr<StructSymbol> resolve_error_struct(const std::string& name) const;
    llvm::Value* generate_error_construction(const FunctionCall& call);
    llvm::Value* generate_interpolated_error_message(const FunctionCall& call);
    void emit_error_propagation_check(const SourceLocation& loc);
    void emit_uncaught_error_check();
    void emit_uncaught_error_trap();
    void emit_div_by_zero_check(const TrapOperand& dividend, const TrapOperand& divisor, const SourceLocation& loc);
    void emit_error_throw_with_tag(int32_t tag, const SourceLocation& loc);
    void store_error_origin(const SourceLocation& loc);

    // Integer overflow modes (w/t/c/s suffixes)
    llvm::Function* get_or_declare_runtime_error_fn();
    llvm::Value* emit_int_arith_with_overflow(TokenType op, const TrapOperand& left, const TrapOperand& right,
                                              bool isSigned, OverflowMode mode, const SourceLocation& loc);
    llvm::Value* emit_saturating_int_arith(TokenType op, llvm::Value* left, llvm::Value* right, bool isSigned);
    llvm::Value* emit_int_neg_with_overflow(const TrapOperand& operand, bool isSigned, OverflowMode mode,
                                            const SourceLocation& loc);

    // Runtime diagnostics: rich traps (source location + operand values +
    // variable assignment history) and shadow call-stack frames for runtime
    // stack traces. Shadow frames, variable tracking and source snippets are
    // emitted in Full mode only; Minimal traps keep file:line + operands.
    void emit_runtime_error_trap(const SourceLocation& loc, const char* message, char op,
                                 const TrapOperand& left, const TrapOperand& right, bool isSigned);
    void emit_runtime_error_trap_min(const SourceLocation& loc, const char* message, char op,
                                     const TrapOperand& left, const TrapOperand& right, bool isSigned);
    void emit_frame_push(const std::string& displayName, const SourceLocation& loc);
    void emit_frame_set_line(const SourceLocation& loc);
    void emit_var_track(llvm::Value* slot, const std::string& name, const SourceLocation& loc);
    void emit_error_stack_capture(const SourceLocation& loc);
    TrapOperand make_trap_operand(const Expression& expr, llvm::Value* value);
    llvm::Constant* cached_global_string(const std::string& text, const char* prefix);
    std::unordered_map<std::string, llvm::Constant*> stringGlobalCache;

    // Contracts (require/ensure)
    std::vector<const ContractClause*> currentContracts_;
    llvm::AllocaInst* contractReturnAlloca = nullptr;
    void setup_contracts(const std::vector<const ContractClause*>& contracts, llvm::Function* llvmFunc);
    void emit_contract_requirements();
    void emit_contract_ensures();
    void emit_non_zero_param_check(const std::string& paramName, const Type& paramType);

    // Non-zero analysis: true when the expression is known to never be zero
    // (non-zero literal, i32n-typed variable, cast to a non-zero type), used
    // to elide division-by-zero checks
    [[nodiscard]] bool is_provably_non_zero(const Expression& expr) const;

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
    void generate_reflection_data();
    static int32_t compute_type_id(const std::string& typeName);
    static uint8_t compute_type_kind(llvm::Type* type, const std::string& typeName);
    llvm::Value* box_value(llvm::Value* value, const std::string& typeName);
    std::string get_type_name_for_value(llvm::Value* value);
    std::string get_djinn_type_name(const Expression& expr, llvm::Value* value);

    // Centralized attribute application
    void apply_attributes(llvm::Function* func, const std::vector<AttributeSymbol>& attrs);
    void apply_implicit_attributes(llvm::Function* func);

    // Parameter attribute injection
    void inject_location_argument(std::vector<llvm::Value*>& args, const SourceLocation& callSite);
};

#endif //DJINN_GENERATOR_H