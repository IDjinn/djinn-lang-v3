//
// Stack-based compile-time VM for constexpr/consteval evaluation
//
// Architecture:
//   1. Compiler: walks AST -> emits bytecode (vector<Instruction>)
//   2. VM: executes bytecode with a value stack (vector<ConstValue>)
//
// Phase 1: Expressions (arithmetic, comparison, logic, cast, unary)
// Phase 2: Statements (if/else, while, for, return) + function calls + local variables
//

#ifndef DJINN_CONST_EVALUATOR_H
#define DJINN_CONST_EVALUATOR_H

#include "ConstValue.h"
#include "../parser/ast/Expression.h"
#include "../parser/ast/Statement.h"
#include "../parser/ast/Declaration.h"
#include "../binder/Symbol.h"
#include <unordered_map>
#include <optional>
#include <string>
#include <vector>
#include <functional>

// --- Bytecode ---

enum class OpCode : uint8_t
{
    // Stack operations
    PUSH_INT, // operand: index into int constant pool
    PUSH_INT128, // operand: index into int128 constant pool
    PUSH_FLOAT, // operand: index into float constant pool
    PUSH_BOOL, // operand: 0 or 1
    POP, // discard top of stack

    // Arithmetic (pop 2, push 1)
    ADD_INT,
    SUB_INT,
    MUL_INT,
    DIV_INT,
    MOD_INT,
    NEG_INT, // unary negate (pop 1, push 1)

    ADD_FLOAT,
    SUB_FLOAT,
    MUL_FLOAT,
    DIV_FLOAT,
    NEG_FLOAT, // unary negate (pop 1, push 1)

    // Comparison (pop 2, push bool)
    CMP_EQ_INT,
    CMP_NE_INT,
    CMP_LT_INT,
    CMP_LE_INT,
    CMP_GT_INT,
    CMP_GE_INT,

    CMP_EQ_FLOAT,
    CMP_NE_FLOAT,
    CMP_LT_FLOAT,
    CMP_LE_FLOAT,
    CMP_GT_FLOAT,
    CMP_GE_FLOAT,

    // Bitwise (pop 2, push 1)
    SHL_INT, // left shift
    SHR_INT, // right shift
    AND_INT, // bitwise and
    OR_INT, // bitwise or
    XOR_INT, // bitwise xor
    NOT_INT, // bitwise not (unary, pop 1, push 1)

    // Logic (pop 1 or 2, push bool)
    LOGIC_AND,
    LOGIC_OR,
    LOGIC_NOT, // unary (pop 1, push 1)

    // Type conversion (pop 1, push 1)
    INT_TO_FLOAT,
    FLOAT_TO_INT,
    BOOL_TO_INT,
    INT_TO_BOOL,

    // Cast with specific bit width (operand: target bits | (signed << 16))
    CAST_INT, // operand encodes target bitWidth and signedness
    CAST_FLOAT, // operand encodes target bitWidth

    // Variables
    STORE_LOCAL, // operand: local slot index; pop value and store
    LOAD_LOCAL, // operand: local slot index; push value

    // Control flow
    JMP, // operand: absolute target instruction index
    JMP_IF_FALSE, // pop top, jump if false; operand: target index
    JMP_IF_TRUE, // pop top, jump if true; operand: target index

    // Functions
    CALL, // operand: function index in function table; pops N args from stack
    RET, // pop top of stack as return value, return to caller

    HALT, // stop execution, top of stack is result
};

struct Instruction
{
    OpCode op;
    uint32_t operand = 0; // meaning depends on opcode

    Instruction(OpCode op, uint32_t operand = 0) : op(op), operand(operand)
    {
    }
};

// Compiled function: bytecode for a constexpr function body
struct CompiledFunction
{
    std::string name;
    uint32_t numParams = 0;
    uint32_t numLocals = 0; // total locals including params
    uint32_t codeStart = 0; // offset into the global instruction stream
};

// --- Bytecode Compiler (AST -> Instructions) ---

class BytecodeCompiler
{
public:
    bool compile(const Expression& expr);
    bool compileFunction(const FunctionDeclaration& func);

    void defineConstant(const std::string& name, ConstValue value);
    uint32_t registerFunction(const std::string& name, const FunctionDeclaration& func);

    [[nodiscard]] const std::vector<Instruction>& instructions() const { return _instructions; }
    [[nodiscard]] const std::vector<int64_t>& intPool() const { return _intPool; }
    [[nodiscard]] const std::vector<Int128>& int128Pool() const { return _int128Pool; }
    [[nodiscard]] const std::vector<double>& floatPool() const { return _floatPool; }
    [[nodiscard]] const std::vector<CompiledFunction>& functionTable() const { return _functionTable; }

    [[nodiscard]] bool hadError() const { return _hadError; }

private:
    std::vector<Instruction> _instructions;
    std::vector<int64_t> _intPool;
    std::vector<Int128> _int128Pool;
    std::vector<double> _floatPool;
    std::vector<CompiledFunction> _functionTable;
    std::unordered_map<std::string, ConstValue> _constants;
    std::unordered_map<std::string, uint32_t> _functionIndex; // name -> index in _functionTable

    // Local variable tracking for current function
    std::unordered_map<std::string, uint32_t> _localSlots;
    uint32_t _nextLocalSlot = 0;

    bool _hadError = false;

    void emit(OpCode op, uint32_t operand = 0);
    uint32_t addIntConstant(int64_t val);
    uint32_t addInt128Constant(Int128 val);
    uint32_t addFloatConstant(double val);
    uint32_t getOrCreateLocal(const std::string& name);

    // Patch a jump instruction's operand to point to current position
    void patchJump(uint32_t instrIndex);
    // Emit a jump and return its index for later patching
    uint32_t emitJump(OpCode op);

    // Expressions
    void compileExpression(const Expression& expr);
    void compileIntegerLiteral(const IntegerLiteral& expr);
    void compileFloatLiteral(const FloatLiteral& expr);
    void compileBooleanLiteral(const BooleanLiteral& expr);
    void compileIdentifier(const Identifier& expr);
    void compileBinaryExpression(const BinaryExpression& expr);
    void compileUnaryExpression(const UnaryExpression& expr);
    void compileCastExpression(const CastExpression& expr);
    void compileFunctionCall(const FunctionCall& expr);
    void compileVariableInit(const VariableInit& expr);
    void compileAssignment(const Assignment& expr);

    // Statements
    void compileStatement(const Statement& stmt);
    void compileBlock(const Block& block);
    void compileReturnStatement(const ReturnStatement& stmt);
    void compileIfStatement(const IfStatement& stmt);
    void compileWhileStatement(const WhileStatement& stmt);
    void compileForStatement(const ForStatement& stmt);
    void compileExpressionStatement(const ExpressionStatement& stmt);
};

// --- VM Configuration ---

struct ConstEvalConfig
{
    uint32_t maxStackSize = 1024; // max value stack depth
    uint32_t maxLocals = 4096; // max local variable slots (across all frames)
    uint32_t maxCallDepth = 512; // max nested function calls
    uint32_t maxIterations = 1000000; // max bytecode instructions before abort
    uint32_t maxHeapSize = 65536; // max heap entries (for future array/struct alloc)
    uint32_t initialHeapSize = 256; // pre-allocated heap slots
};

// --- Stack-based VM ---

struct CallFrame
{
    uint32_t returnIP = 0; // instruction to return to
    uint32_t localsBase = 0; // index into locals array where this frame's locals start
    uint32_t numLocals = 0; // how many locals this frame has
};

class ConstVM
{
public:
    explicit ConstVM(const ConstEvalConfig& config = {}) : _config(config)
    {
    }

    ConstValue execute(
        const std::vector<Instruction>& code,
        const std::vector<int64_t>& intPool,
        const std::vector<Int128>& int128Pool,
        const std::vector<double>& floatPool,
        const std::vector<CompiledFunction>& functions
    );

private:
    ConstEvalConfig _config;
    std::vector<ConstValue> _stack;
    std::vector<ConstValue> _locals;
    std::vector<CallFrame> _callFrames;
    std::vector<ConstValue> _heap; // future: heap-allocated values (arrays, structs)
    uint32_t _currentLocalsBase = 0;

    void push(ConstValue val);
    ConstValue pop();
    [[nodiscard]] ConstValue& top();
};

// --- Public API ---

class ConstEvaluator
{
public:
    explicit ConstEvaluator(const ConstEvalConfig& config = {}) : _config(config)
    {
    }

    // Evaluate an expression at compile time. Returns ConstValue::Error if not evaluable.
    ConstValue evaluate(const Expression& expr);

    // Evaluate a constexpr function call with given constant arguments
    ConstValue evaluateFunction(const FunctionDeclaration& func, const std::vector<ConstValue>& args);

    // Register a known compile-time constant
    void defineConstant(const std::string& name, ConstValue value);

    // Register a constexpr function (so it can be called from other constexpr contexts)
    void defineFunction(const std::string& name, const FunctionDeclaration& func);

    // Check if a name is a known constant
    [[nodiscard]] std::optional<ConstValue> lookupConstant(const std::string& name) const;

    // Configuration access
    [[nodiscard]] const ConstEvalConfig& config() const { return _config; }

private:
    ConstEvalConfig _config;
    std::unordered_map<std::string, ConstValue> _constants;
    std::unordered_map<std::string, const FunctionDeclaration*> _functions;

    void syncToCompiler(BytecodeCompiler& compiler) const;
};

#endif // DJINN_CONST_EVALUATOR_H