//
// Stack-based compile-time VM implementation — Phase 2
//

#include "ConstEvaluator.h"
#include "../utils/Logger.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    struct IntLimits
    {
        int64_t min;
        int64_t max;
        uint64_t umax;
    };

    IntLimits limitsFor(const unsigned bits, const bool isSigned)
    {
        IntLimits l;
        if (bits >= 64)
        {
            l.min = INT64_MIN;
            l.max = INT64_MAX;
            l.umax = UINT64_MAX;
        }
        else
        {
            const uint64_t mask = (1ULL << bits) - 1;
            l.umax = mask;
            l.max = isSigned ? static_cast<int64_t>(mask >> 1) : static_cast<int64_t>(mask);
            l.min = isSigned ? static_cast<int64_t>(~(mask >> 1)) : 0;
        }
        return l;
    }

    bool addOverflows64(const int64_t a, const int64_t b)
    {
        return (b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b);
    }

    bool subOverflows64(const int64_t a, const int64_t b)
    {
        return (b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b);
    }

    bool mulOverflows64(const int64_t a, const int64_t b)
    {
        if (a == 0 || b == 0) return false;
        if (a == INT64_MIN || b == INT64_MIN) return true;
        const int64_t r = a * b;
        return r / b != a;
    }

    // Fits `raw` (may already have wrapped at 64-bit when overflowHint is true)
    // into bits/sign applying the overflow mode. Returns error for t/c on
    // overflow, clamps for s, wraps for w/none.
    ConstValue fitInt(int64_t raw, const bool overflowHint, const bool negative,
                      const unsigned bits, const bool isSigned, const OverflowMode mode)
    {
        const IntLimits lim = limitsFor(bits, isSigned);

        bool out = overflowHint;
        if (!out)
        {
            if (isSigned) out = raw < lim.min || raw > lim.max;
            else out = static_cast<uint64_t>(raw) > lim.umax;
        }

        if (out && mode != OverflowMode::None && mode != OverflowMode::Wrapped)
        {
            if (mode == OverflowMode::Trapped || mode == OverflowMode::Checked)
            {
                return ConstValue::error();
            }
            return ConstValue::makeInt(negative ? lim.min : lim.max, bits, isSigned);
        }

        // wrap to width (two's complement)
        if (bits < 64)
        {
            const uint64_t mask = (1ULL << bits) - 1;
            uint64_t u = static_cast<uint64_t>(raw) & mask;
            if (isSigned && (u & (1ULL << (bits - 1)))) u |= ~mask;
            raw = static_cast<int64_t>(u);
        }
        return ConstValue::makeInt(raw, bits, isSigned);
    }
}

// ============================================================
// BytecodeCompiler: helpers
// ============================================================

void BytecodeCompiler::emit(OpCode op, uint32_t operand)
{
    _instructions.emplace_back(op, operand);
}

uint32_t BytecodeCompiler::addIntConstant(int64_t val)
{
    for (uint32_t i = 0; i < _intPool.size(); i++)
        if (_intPool[i] == val) return i;
    _intPool.push_back(val);
    return static_cast<uint32_t>(_intPool.size() - 1);
}

uint32_t BytecodeCompiler::addInt128Constant(Int128 val)
{
    for (uint32_t i = 0; i < _int128Pool.size(); i++)
        if (_int128Pool[i] == val) return i;
    _int128Pool.push_back(val);
    return static_cast<uint32_t>(_int128Pool.size() - 1);
}

uint32_t BytecodeCompiler::addFloatConstant(double val)
{
    for (uint32_t i = 0; i < _floatPool.size(); i++)
        if (_floatPool[i] == val) return i;
    _floatPool.push_back(val);
    return static_cast<uint32_t>(_floatPool.size() - 1);
}

uint32_t BytecodeCompiler::addStringConstant(const std::string& val)
{
    for (uint32_t i = 0; i < _stringPool.size(); i++)
        if (_stringPool[i] == val) return i;
    _stringPool.push_back(val);
    return static_cast<uint32_t>(_stringPool.size() - 1);
}

uint32_t BytecodeCompiler::getOrCreateLocal(const std::string& name)
{
    auto it = _localSlots.find(name);
    if (it != _localSlots.end()) return it->second;
    uint32_t slot = _nextLocalSlot++;
    _localSlots[name] = slot;
    return slot;
}

uint32_t BytecodeCompiler::emitJump(OpCode op)
{
    uint32_t idx = static_cast<uint32_t>(_instructions.size());
    emit(op, 0xFFFFFFFF); // placeholder
    return idx;
}

void BytecodeCompiler::patchJump(uint32_t instrIndex)
{
    _instructions[instrIndex].operand = static_cast<uint32_t>(_instructions.size());
}

// ============================================================
// BytecodeCompiler: compile expression (entry point)
// ============================================================

bool BytecodeCompiler::compile(const Expression& expr)
{
    _instructions.clear();
    _intPool.clear();
    _int128Pool.clear();
    _floatPool.clear();
    _stringPool.clear();
    _localSlots.clear();
    _nextLocalSlot = 0;
    _hadError = false;

    compileExpression(expr);
    if (!_hadError) emit(OpCode::HALT);
    return !_hadError;
}

void BytecodeCompiler::defineConstant(const std::string& name, ConstValue value)
{
    _constants[name] = value;
}

uint32_t BytecodeCompiler::registerFunction(const std::string& name, const FunctionDeclaration& func)
{
    std::vector<std::string> paramNames;
    for (const auto& param : func.parameters)
        paramNames.push_back(param.name.token_name);

    if (!func.body)
    {
        // Declaration only: register an entry that errors if ever called
        auto it = _functionIndex.find(name);
        if (it != _functionIndex.end()) return it->second;

        const uint32_t idx = static_cast<uint32_t>(_functionTable.size());
        _functionIndex[name] = idx;

        CompiledFunction cf;
        cf.name = name;
        cf.numParams = static_cast<uint32_t>(paramNames.size());
        cf.codeStart = CompiledFunction::InvalidCodeStart;
        _functionTable.push_back(cf);
        return idx;
    }

    return registerFunction(name, paramNames, *func.body);
}

uint32_t BytecodeCompiler::registerFunction(const std::string& name, const std::vector<std::string>& paramNames,
                                            const Block& body)
{
    // Check if already registered
    auto it = _functionIndex.find(name);
    if (it != _functionIndex.end()) return it->second;

    uint32_t idx = static_cast<uint32_t>(_functionTable.size());
    _functionIndex[name] = idx;

    CompiledFunction cf;
    cf.name = name;
    cf.numParams = static_cast<uint32_t>(paramNames.size());
    _functionTable.push_back(cf);

    ConstFunction pending;
    pending.paramNames = paramNames;
    pending.body = &body;
    _pendingFunctions[name] = pending;

    return idx;
}

bool BytecodeCompiler::compileFunction(const std::string& name)
{
    auto idxIt = _functionIndex.find(name);
    if (idxIt == _functionIndex.end())
    {
        _hadError = true;
        return false;
    }
    const uint32_t funcIdx = idxIt->second;

    auto pendIt = _pendingFunctions.find(name);
    if (pendIt == _pendingFunctions.end() || !pendIt->second.body)
    {
        _functionTable[funcIdx].codeStart = CompiledFunction::InvalidCodeStart;
        _hadError = true;
        return false;
    }

    _hadError = false;

    // Save and reset local state
    auto savedLocals = std::move(_localSlots);
    uint32_t savedNextSlot = _nextLocalSlot;
    _localSlots.clear();
    _nextLocalSlot = 0;

    // Record code start
    _functionTable[funcIdx].codeStart = static_cast<uint32_t>(_instructions.size());

    // Allocate slots for parameters
    for (const auto& paramName : pendIt->second.paramNames)
    {
        getOrCreateLocal(paramName);
    }

    // Compile body
    compileBlock(*pendIt->second.body);

    // Implicit return void if no explicit return
    emit(OpCode::PUSH_INT, addIntConstant(0));
    emit(OpCode::RET);

    _functionTable[funcIdx].numLocals = _nextLocalSlot;

    // Restore local state
    _localSlots = std::move(savedLocals);
    _nextLocalSlot = savedNextSlot;

    if (_hadError)
    {
        // Unreachable: CALL rejects invalid entries
        _functionTable[funcIdx].codeStart = CompiledFunction::InvalidCodeStart;
        return false;
    }
    return true;
}

bool BytecodeCompiler::compileAllFunctions()
{
    bool all = true;
    for (const auto& [name, pending] : _pendingFunctions)
    {
        if (!compileFunction(name)) all = false;
    }
    return all;
}

bool BytecodeCompiler::compileCallDriver(const std::string& name, const std::vector<ConstValue>& args)
{
    auto it = _functionIndex.find(name);
    if (it == _functionIndex.end())
    {
        LOG_DEBUG("[consteval] function '%s' not registered for constexpr", name.c_str());
        return false;
    }

    for (const auto& arg : args)
    {
        switch (arg.kind)
        {
        case ConstValue::Integer: emit(OpCode::PUSH_INT, addIntConstant(arg.intVal));
            break;
        case ConstValue::Integer128: emit(OpCode::PUSH_INT128, addInt128Constant(arg.int128Val));
            break;
        case ConstValue::Float: emit(OpCode::PUSH_FLOAT, addFloatConstant(arg.floatVal));
            break;
        case ConstValue::Bool: emit(OpCode::PUSH_BOOL, arg.boolVal ? 1 : 0);
            break;
        default:
            LOG_DEBUG("[consteval] argument of kind %d not supported as call argument", static_cast<int>(arg.kind));
            return false;
        }
    }

    emit(OpCode::CALL, it->second);
    emit(OpCode::HALT);
    return true;
}

// ============================================================
// BytecodeCompiler: expressions
// ============================================================

void BytecodeCompiler::compileExpression(const Expression& expr)
{
    if (_hadError) return;

    if (const auto* intLit = dynamic_cast<const IntegerLiteral*>(&expr))
        return compileIntegerLiteral(*intLit);
    if (const auto* floatLit = dynamic_cast<const FloatLiteral*>(&expr))
        return compileFloatLiteral(*floatLit);
    if (const auto* boolLit = dynamic_cast<const BooleanLiteral*>(&expr))
        return compileBooleanLiteral(*boolLit);
    if (const auto* ident = dynamic_cast<const Identifier*>(&expr))
        return compileIdentifier(*ident);
    if (const auto* fa = dynamic_cast<const FieldAccess*>(&expr))
    {
        if (const auto* obj = dynamic_cast<const Identifier*>(fa->object.get()))
        {
            Identifier syntheticId(SourceIdentifier(obj->name() + "." + fa->fieldName.token_name, fa->location));
            return compileIdentifier(syntheticId);
        }
        _hadError = true;
        return;
    }
    if (const auto* binExpr = dynamic_cast<const BinaryExpression*>(&expr))
        return compileBinaryExpression(*binExpr);
    if (const auto* unaryExpr = dynamic_cast<const UnaryExpression*>(&expr))
        return compileUnaryExpression(*unaryExpr);
    if (const auto* castExpr = dynamic_cast<const CastExpression*>(&expr))
        return compileCastExpression(*castExpr);
    if (const auto* callExpr = dynamic_cast<const FunctionCall*>(&expr))
        return compileFunctionCall(*callExpr);
    if (const auto* varInit = dynamic_cast<const VariableInit*>(&expr))
        return compileVariableInit(*varInit);
    if (const auto* assign = dynamic_cast<const Assignment*>(&expr))
        return compileAssignment(*assign);

    LOG_DEBUG("[consteval] unsupported expression type for compile-time evaluation");
    _hadError = true;
}

void BytecodeCompiler::compileIntegerLiteral(const IntegerLiteral& expr)
{
    std::string cleaned;
    cleaned.reserve(expr.value.size());
    for (char c : expr.value)
        if (c != '_' && c != '\'') cleaned += c;

    bool isUnsigned = false;
    if (!cleaned.empty() && (cleaned.back() == 'u' || cleaned.back() == 'U'))
    {
        isUnsigned = true;
        cleaned.pop_back();
    }

    int radix = 10;
    if (cleaned.size() > 2 && cleaned[0] == '0')
    {
        if (cleaned[1] == 'x' || cleaned[1] == 'X')
        {
            radix = 16;
            cleaned = cleaned.substr(2);
        }
        else if (cleaned[1] == 'b' || cleaned[1] == 'B')
        {
            radix = 2;
            cleaned = cleaned.substr(2);
        }
    }

    if (radix == 10)
    {
        auto ePos = cleaned.find_first_of("eE");
        if (ePos != std::string::npos)
        {
            double base = std::stod(cleaned.substr(0, ePos));
            double exp = std::stod(cleaned.substr(ePos + 1));
            emit(OpCode::PUSH_INT, addIntConstant(static_cast<int64_t>(base * std::pow(10.0, exp))));
            return;
        }
    }

    try
    {
        int64_t val = std::stoll(cleaned, nullptr, radix);
        emit(OpCode::PUSH_INT, addIntConstant(val));
    }
    catch (const std::out_of_range&)
    {
        // Value exceeds 64 bits — parse as 128-bit integer
        // Parse hex string manually into high:low pair
        Int128 val128;
        if (radix == 16)
        {
            // Split hex string into high and low 16-char halves
            if (cleaned.size() <= 16)
            {
                val128.low = std::stoull(cleaned, nullptr, 16);
                val128.high = 0;
            }
            else
            {
                std::string lowStr = cleaned.substr(cleaned.size() - 16);
                std::string highStr = cleaned.substr(0, cleaned.size() - 16);
                val128.low = std::stoull(lowStr, nullptr, 16);
                val128.high = static_cast<int64_t>(std::stoull(highStr, nullptr, 16));
            }
        }
        else
        {
            LOG_DEBUG("[consteval] 128-bit literals only supported in hex: %s", cleaned.c_str());
            _hadError = true;
            return;
        }
        emit(OpCode::PUSH_INT128, addInt128Constant(val128));
    }
}

void BytecodeCompiler::compileFloatLiteral(const FloatLiteral& expr)
{
    try
    {
        double val = std::stod(expr.value);
        emit(OpCode::PUSH_FLOAT, addFloatConstant(val));
    }
    catch (const std::out_of_range&)
    {
        LOG_DEBUG("[consteval] float literal out of range: %s", expr.value.c_str());
        _hadError = true;
    }
}

void BytecodeCompiler::compileBooleanLiteral(const BooleanLiteral& expr)
{
    emit(OpCode::PUSH_BOOL, expr.value == "true" ? 1 : 0);
}

void BytecodeCompiler::compileIdentifier(const Identifier& expr)
{
    const std::string& name = expr.name();

    // Check local variables first
    auto localIt = _localSlots.find(name);
    if (localIt != _localSlots.end())
    {
        emit(OpCode::LOAD_LOCAL, localIt->second);
        return;
    }

    // Check named constants
    auto constIt = _constants.find(name);
    if (constIt != _constants.end())
    {
        const ConstValue& val = constIt->second;
        switch (val.kind)
        {
        case ConstValue::Integer: emit(OpCode::PUSH_INT, addIntConstant(val.intVal));
            break;
        case ConstValue::Integer128: emit(OpCode::PUSH_INT128, addInt128Constant(val.int128Val));
            break;
        case ConstValue::Float: emit(OpCode::PUSH_FLOAT, addFloatConstant(val.floatVal));
            break;
        case ConstValue::Bool: emit(OpCode::PUSH_BOOL, val.boolVal ? 1 : 0);
            break;
        default: _hadError = true;
            break;
        }
        return;
    }

    LOG_DEBUG("[consteval] identifier '%s' not found", name.c_str());
    _hadError = true;
}

void BytecodeCompiler::compileBinaryExpression(const BinaryExpression& expr)
{
    compileExpression(*expr.left);
    if (_hadError) return;
    compileExpression(*expr.right);
    if (_hadError) return;

    // operand carries the overflow mode set by the binder (w/t/c/s)
    const auto modeOp = [](const BinaryExpression& e) -> uint32_t
    {
        return static_cast<uint32_t>(e.overflowMode);
    };

    switch (expr.op)
    {
    case TokenType::PLUS: emit(OpCode::ADD_INT, modeOp(expr));
        break;
    case TokenType::MINUS: emit(OpCode::SUB_INT, modeOp(expr));
        break;
    case TokenType::STAR: emit(OpCode::MUL_INT, modeOp(expr));
        break;
    case TokenType::SLASH: emit(OpCode::DIV_INT, modeOp(expr));
        break;
    case TokenType::PERCENT: emit(OpCode::MOD_INT, modeOp(expr));
        break;
    case TokenType::EQUAL_EQUAL: emit(OpCode::CMP_EQ_INT);
        break;
    case TokenType::BANG_EQUAL: emit(OpCode::CMP_NE_INT);
        break;
    case TokenType::LESS: emit(OpCode::CMP_LT_INT);
        break;
    case TokenType::LESS_EQUAL: emit(OpCode::CMP_LE_INT);
        break;
    case TokenType::GREATER: emit(OpCode::CMP_GT_INT);
        break;
    case TokenType::GREATER_EQUAL: emit(OpCode::CMP_GE_INT);
        break;
    case TokenType::LESS_LESS: emit(OpCode::SHL_INT);
        break;
    case TokenType::GREATER_GREATER: emit(OpCode::SHR_INT);
        break;
    case TokenType::AMPERSAND: emit(OpCode::AND_INT);
        break;
    case TokenType::PIPE: emit(OpCode::OR_INT);
        break;
    case TokenType::CARET: emit(OpCode::XOR_INT);
        break;
    case TokenType::AND_AND: emit(OpCode::LOGIC_AND);
        break;
    case TokenType::OR_OR: emit(OpCode::LOGIC_OR);
        break;
    default: _hadError = true;
        break;
    }
}

void BytecodeCompiler::compileUnaryExpression(const UnaryExpression& expr)
{
    compileExpression(*expr.operand);
    if (_hadError) return;

    switch (expr.op)
    {
    case TokenType::MINUS: emit(OpCode::NEG_INT, static_cast<uint32_t>(expr.overflowMode));
        break;
    case TokenType::BANG: emit(OpCode::LOGIC_NOT);
        break;
    case TokenType::TILDE: emit(OpCode::NOT_INT);
        break;
    default: _hadError = true;
        break;
    }
}

void BytecodeCompiler::compileCastExpression(const CastExpression& expr)
{
    compileExpression(*expr.operand);
    if (_hadError) return;

    const Type& target = expr.targetType;
    if (target.kind == TypeKind::INTEGER)
    {
        uint32_t operand = static_cast<uint32_t>(target.size) | (target.sign ? (1u << 16) : 0);
        emit(OpCode::CAST_INT, operand);
    }
    else if (target.kind == TypeKind::F32 || target.kind == TypeKind::F64)
    {
        emit(OpCode::CAST_FLOAT, (target.kind == TypeKind::F32) ? 32 : 64);
    }
    else
    {
        _hadError = true;
    }
}

void BytecodeCompiler::compileFunctionCall(const FunctionCall& expr)
{
    const std::string& name = expr.name.token_name;

    // Branch-prediction intrinsics are identity at compile time
    if ((name == "likely" || name == "unlikely") && expr.arguments.size() == 1)
    {
        compileExpression(*expr.arguments[0]);
        return;
    }

    auto it = _functionIndex.find(name);
    if (it == _functionIndex.end())
    {
        LOG_DEBUG("[consteval] function '%s' not registered for constexpr", name.c_str());
        _hadError = true;
        return;
    }

    // Push arguments onto stack (left to right)
    for (const auto& arg : expr.arguments)
    {
        compileExpression(*arg);
        if (_hadError) return;
    }

    emit(OpCode::CALL, it->second);
}

void BytecodeCompiler::compileVariableInit(const VariableInit& expr)
{
    compileExpression(*expr.value);
    if (_hadError) return;

    uint32_t slot = getOrCreateLocal(expr.name.token_name);
    emit(OpCode::STORE_LOCAL, slot);
}

void BytecodeCompiler::compileAssignment(const Assignment& expr)
{
    compileExpression(*expr.value);
    if (_hadError) return;

    auto it = _localSlots.find(expr.name.token_name);
    if (it == _localSlots.end())
    {
        LOG_DEBUG("[consteval] assignment to unknown variable '%s'", expr.name.token_name.c_str());
        _hadError = true;
        return;
    }
    emit(OpCode::STORE_LOCAL, it->second);
}

// ============================================================
// BytecodeCompiler: statements
// ============================================================

void BytecodeCompiler::compileStatement(const Statement& stmt)
{
    if (_hadError) return;

    if (const auto* retStmt = dynamic_cast<const ReturnStatement*>(&stmt))
        return compileReturnStatement(*retStmt);
    if (const auto* throwStmt = dynamic_cast<const ThrowStatement*>(&stmt))
        return compileThrowStatement(*throwStmt);
    if (const auto* ifStmt = dynamic_cast<const IfStatement*>(&stmt))
        return compileIfStatement(*ifStmt);
    if (const auto* whileStmt = dynamic_cast<const WhileStatement*>(&stmt))
        return compileWhileStatement(*whileStmt);
    if (const auto* forStmt = dynamic_cast<const ForStatement*>(&stmt))
        return compileForStatement(*forStmt);
    if (const auto* block = dynamic_cast<const Block*>(&stmt))
        return compileBlock(*block);
    if (const auto* exprStmt = dynamic_cast<const ExpressionStatement*>(&stmt))
        return compileExpressionStatement(*exprStmt);

    LOG_DEBUG("[consteval] unsupported statement type");
    _hadError = true;
}

void BytecodeCompiler::compileBlock(const Block& block)
{
    for (const auto& stmt : block.statements)
    {
        compileStatement(*stmt);
        if (_hadError) return;
    }
}

void BytecodeCompiler::compileReturnStatement(const ReturnStatement& stmt)
{
    if (stmt.value)
    {
        compileExpression(*stmt.value);
    }
    else
    {
        emit(OpCode::PUSH_INT, addIntConstant(0)); // void return
    }
    emit(OpCode::RET);
}

void BytecodeCompiler::compileThrowStatement(const ThrowStatement& stmt)
{
    // The thrown error type is recorded statically; the payload expression
    // (interpolated strings, constructors) is not const-evaluable, so it is
    // never compiled. THROW terminates the VM with a Thrown result.
    std::string errorName;
    if (stmt.expression)
    {
        if (const auto* call = dynamic_cast<const FunctionCall*>(stmt.expression.get()))
            errorName = call->name.token_name;
        else if (const auto* ident = dynamic_cast<const Identifier*>(stmt.expression.get()))
            errorName = ident->name();
    }
    emit(OpCode::THROW, addStringConstant(errorName));
}

void BytecodeCompiler::compileIfStatement(const IfStatement& stmt)
{
    // Compile condition
    compileExpression(*stmt.condition);
    if (_hadError) return;

    // JMP_IF_FALSE -> else branch (or end)
    uint32_t jumpToElse = emitJump(OpCode::JMP_IF_FALSE);

    // Then branch
    if (stmt.thenBranch) compileBlock(*stmt.thenBranch);

    if (stmt.elseBranch)
    {
        // JMP over else
        uint32_t jumpToEnd = emitJump(OpCode::JMP);
        patchJump(jumpToElse);
        compileBlock(*stmt.elseBranch);
        patchJump(jumpToEnd);
    }
    else
    {
        patchJump(jumpToElse);
    }
}

void BytecodeCompiler::compileWhileStatement(const WhileStatement& stmt)
{
    uint32_t loopStart = static_cast<uint32_t>(_instructions.size());

    compileExpression(*stmt.condition);
    if (_hadError) return;

    uint32_t jumpToEnd = emitJump(OpCode::JMP_IF_FALSE);

    if (stmt.body) compileBlock(*stmt.body);

    emit(OpCode::JMP, loopStart);
    patchJump(jumpToEnd);
}

void BytecodeCompiler::compileForStatement(const ForStatement& stmt)
{
    // Initializer
    if (stmt.initializer) compileExpression(*stmt.initializer);

    uint32_t loopStart = static_cast<uint32_t>(_instructions.size());

    // Condition
    uint32_t jumpToEnd = 0;
    if (stmt.condition)
    {
        compileExpression(*stmt.condition);
        if (_hadError) return;
        jumpToEnd = emitJump(OpCode::JMP_IF_FALSE);
    }

    // Body
    if (stmt.body) compileBlock(*stmt.body);

    // Postfix (e.g. i = i + 1)
    if (stmt.postfix) compileExpression(*stmt.postfix);

    emit(OpCode::JMP, loopStart);

    if (stmt.condition) patchJump(jumpToEnd);
}

void BytecodeCompiler::compileExpressionStatement(const ExpressionStatement& stmt)
{
    compileExpression(*stmt.expression);
    if (_hadError) return;
    // Discard result (expression statements don't push to stack permanently)
    // But VariableInit and Assignment already handle their own storage via STORE_LOCAL
    // Only discard if the expression produces a value that's not consumed
    // For simplicity: if it's not VariableInit or Assignment, pop
    if (!dynamic_cast<const VariableInit*>(stmt.expression.get()) &&
        !dynamic_cast<const Assignment*>(stmt.expression.get()))
    {
        emit(OpCode::POP);
    }
}

// ============================================================
// ConstVM: Execute bytecode on a value stack
// ============================================================

void ConstVM::push(ConstValue val)
{
    if (_stack.size() >= _config.maxStackSize)
    {
        LOG_DEBUG("[consteval] VM stack overflow (max %u)", _config.maxStackSize);
        throw std::runtime_error("consteval VM: stack overflow");
    }
    _stack.push_back(val);
}

ConstValue ConstVM::pop()
{
    if (_stack.empty()) throw std::runtime_error("consteval VM: stack underflow");
    ConstValue val = _stack.back();
    _stack.pop_back();
    return val;
}

ConstValue& ConstVM::top()
{
    if (_stack.empty()) throw std::runtime_error("consteval VM: stack underflow on top()");
    return _stack.back();
}

ConstValue ConstVM::execute(
    const std::vector<Instruction>& code,
    const std::vector<int64_t>& intPool,
    const std::vector<Int128>& int128Pool,
    const std::vector<double>& floatPool,
    const std::vector<std::string>& stringPool,
    const std::vector<CompiledFunction>& functions)
{
    _stack.clear();
    _stack.reserve(std::min(_config.maxStackSize, 256u));
    _locals.clear();
    _locals.resize(std::min(_config.initialHeapSize, _config.maxLocals), ConstValue::makeInt(0));
    _heap.clear();
    _heap.reserve(_config.initialHeapSize);
    _callFrames.clear();
    _currentLocalsBase = 0;

    size_t ip = 0;
    const size_t codeSize = code.size();
    uint32_t iterations = 0;

    while (ip < codeSize)
    {
        if (++iterations > _config.maxIterations)
        {
            LOG_DEBUG("[consteval] VM exceeded max iterations (%u)", _config.maxIterations);
            return ConstValue::error();
        }

        const Instruction& instr = code[ip];

        switch (instr.op)
        {
        case OpCode::PUSH_INT:
            {
                const int64_t v = intPool[instr.operand];
                // Width follows magnitude so 64-bit constants (e.g. nint.MAX_VALUE)
                // don't silently truncate to i32 when materialized
                const unsigned bits = (v >= INT32_MIN && v <= INT32_MAX) ? 32 : 64;
                push(ConstValue::makeInt(v, bits));
            }
            break;
        case OpCode::PUSH_INT128: push(ConstValue::makeInt128(int128Pool[instr.operand]));
            break;
        case OpCode::PUSH_FLOAT: push(ConstValue::makeFloat(floatPool[instr.operand]));
            break;
        case OpCode::PUSH_BOOL: push(ConstValue::makeBool(instr.operand != 0));
            break;
        case OpCode::POP: pop();
            break;

        // --- Arithmetic (with auto int/float promotion) ---
        case OpCode::ADD_INT:
            {
                auto r = pop();
                auto l = pop();
                if (l.kind == ConstValue::Float || r.kind == ConstValue::Float)
                    push(ConstValue::makeFloat(l.toFloat() + r.toFloat()));
                else
                {
                    const auto mode = static_cast<OverflowMode>(instr.operand);
                    const int64_t a = l.toInt(), b = r.toInt();
                    const bool ovf = addOverflows64(a, b);
                    push(fitInt(a + b, ovf, a < 0 && b < 0,
                                std::max(l.bitWidth, r.bitWidth), l.isSigned && r.isSigned, mode));
                }
                break;
            }
        case OpCode::SUB_INT:
            {
                auto r = pop();
                auto l = pop();
                if (l.kind == ConstValue::Float || r.kind == ConstValue::Float)
                    push(ConstValue::makeFloat(l.toFloat() - r.toFloat()));
                else
                {
                    const auto mode = static_cast<OverflowMode>(instr.operand);
                    const int64_t a = l.toInt(), b = r.toInt();
                    const bool ovf = subOverflows64(a, b);
                    push(fitInt(a - b, ovf, a < 0 && b > 0,
                                std::max(l.bitWidth, r.bitWidth), l.isSigned && r.isSigned, mode));
                }
                break;
            }
        case OpCode::MUL_INT:
            {
                auto r = pop();
                auto l = pop();
                if (l.kind == ConstValue::Float || r.kind == ConstValue::Float)
                    push(ConstValue::makeFloat(l.toFloat() * r.toFloat()));
                else
                {
                    const auto mode = static_cast<OverflowMode>(instr.operand);
                    const int64_t a = l.toInt(), b = r.toInt();
                    const bool ovf = mulOverflows64(a, b);
                    push(fitInt(a * b, ovf, (a < 0) != (b < 0),
                                std::max(l.bitWidth, r.bitWidth), l.isSigned && r.isSigned, mode));
                }
                break;
            }
        case OpCode::DIV_INT:
            {
                auto r = pop();
                auto l = pop();
                if (l.kind == ConstValue::Float || r.kind == ConstValue::Float)
                {
                    double rd = r.toFloat();
                    if (rd == 0.0) return ConstValue::error();
                    push(ConstValue::makeFloat(l.toFloat() / rd));
                }
                else
                {
                    const auto mode = static_cast<OverflowMode>(instr.operand);
                    const int64_t a = l.toInt(), b = r.toInt();
                    if (b == 0) return ConstValue::error();

                    // INT_MIN / -1 overflows (and is UB in C++)
                    if (a == INT64_MIN && b == -1)
                    {
                        if (mode == OverflowMode::Trapped || mode == OverflowMode::Checked)
                            return ConstValue::error();
                        if (mode == OverflowMode::Saturating)
                        {
                            push(ConstValue::makeInt(
                                INT64_MAX, std::max(l.bitWidth, r.bitWidth), l.isSigned && r.isSigned));
                            break;
                        }
                    }
                    const int64_t q = (a == INT64_MIN && b == -1) ? INT64_MIN : a / b;
                    push(fitInt(q, false, false,
                                std::max(l.bitWidth, r.bitWidth), l.isSigned && r.isSigned, mode));
                }
                break;
            }
        case OpCode::MOD_INT:
            {
                auto r = pop();
                auto l = pop();
                const auto mode = static_cast<OverflowMode>(instr.operand);
                const int64_t a = l.toInt();
                const int64_t ri = r.toInt();
                if (ri == 0) return ConstValue::error();

                // INT_MIN % -1 == 0 mathematically (and is UB in C++)
                const int64_t m = (a == INT64_MIN && ri == -1) ? 0 : a % ri;
                push(fitInt(m, false, false,
                            std::max(l.bitWidth, r.bitWidth), l.isSigned && r.isSigned, mode));
                break;
            }
        case OpCode::NEG_INT:
            {
                auto v = pop();
                if (v.kind == ConstValue::Float)
                    push(ConstValue::makeFloat(-v.floatVal, v.bitWidth));
                else
                {
                    const auto mode = static_cast<OverflowMode>(instr.operand);
                    const int64_t x = v.toInt();
                    const bool ovf = x == INT64_MIN || (
                        v.bitWidth < 64 && x == limitsFor(v.bitWidth, v.isSigned).min);
                    // -MIN overflows positively: saturates to MAX
                    const int64_t raw = (x == INT64_MIN) ? INT64_MIN : -x;
                    const auto fitted = fitInt(raw, ovf, false, v.bitWidth, v.isSigned, mode);
                    if (fitted.isError()) return fitted;
                    push(fitted);
                }
                break;
            }

        // Float arithmetic
        case OpCode::ADD_FLOAT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeFloat(l.toFloat() + r.toFloat()));
                break;
            }
        case OpCode::SUB_FLOAT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeFloat(l.toFloat() - r.toFloat()));
                break;
            }
        case OpCode::MUL_FLOAT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeFloat(l.toFloat() * r.toFloat()));
                break;
            }
        case OpCode::DIV_FLOAT:
            {
                auto r = pop();
                auto l = pop();
                double rd = r.toFloat();
                if (rd == 0.0) return ConstValue::error();
                push(ConstValue::makeFloat(l.toFloat() / rd));
                break;
            }
        case OpCode::NEG_FLOAT:
            {
                auto v = pop();
                push(ConstValue::makeFloat(-v.toFloat(), v.bitWidth));
                break;
            }

        // Comparisons
        case OpCode::CMP_EQ_INT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeBool(l.toInt() == r.toInt()));
                break;
            }
        case OpCode::CMP_NE_INT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeBool(l.toInt() != r.toInt()));
                break;
            }
        case OpCode::CMP_LT_INT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeBool(l.toInt() < r.toInt()));
                break;
            }
        case OpCode::CMP_LE_INT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeBool(l.toInt() <= r.toInt()));
                break;
            }
        case OpCode::CMP_GT_INT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeBool(l.toInt() > r.toInt()));
                break;
            }
        case OpCode::CMP_GE_INT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeBool(l.toInt() >= r.toInt()));
                break;
            }

        case OpCode::CMP_EQ_FLOAT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeBool(l.toFloat() == r.toFloat()));
                break;
            }
        case OpCode::CMP_NE_FLOAT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeBool(l.toFloat() != r.toFloat()));
                break;
            }
        case OpCode::CMP_LT_FLOAT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeBool(l.toFloat() < r.toFloat()));
                break;
            }
        case OpCode::CMP_LE_FLOAT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeBool(l.toFloat() <= r.toFloat()));
                break;
            }
        case OpCode::CMP_GT_FLOAT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeBool(l.toFloat() > r.toFloat()));
                break;
            }
        case OpCode::CMP_GE_FLOAT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeBool(l.toFloat() >= r.toFloat()));
                break;
            }

        // Logic
        case OpCode::SHL_INT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeInt(l.toInt() << r.toInt()));
                break;
            }
        case OpCode::SHR_INT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeInt(l.toInt() >> r.toInt()));
                break;
            }
        case OpCode::AND_INT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeInt(l.toInt() & r.toInt()));
                break;
            }
        case OpCode::OR_INT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeInt(l.toInt() | r.toInt()));
                break;
            }
        case OpCode::XOR_INT:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeInt(l.toInt() ^ r.toInt()));
                break;
            }
        case OpCode::NOT_INT:
            {
                auto v = pop();
                push(ConstValue::makeInt(~v.toInt()));
                break;
            }
        case OpCode::LOGIC_AND:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeBool(l.toBool() && r.toBool()));
                break;
            }
        case OpCode::LOGIC_OR:
            {
                auto r = pop();
                auto l = pop();
                push(ConstValue::makeBool(l.toBool() || r.toBool()));
                break;
            }
        case OpCode::LOGIC_NOT:
            {
                auto v = pop();
                push(ConstValue::makeBool(!v.toBool()));
                break;
            }

        // Conversions
        case OpCode::INT_TO_FLOAT:
            {
                auto v = pop();
                push(ConstValue::makeFloat(static_cast<double>(v.intVal)));
                break;
            }
        case OpCode::FLOAT_TO_INT:
            {
                auto v = pop();
                push(ConstValue::makeInt(static_cast<int64_t>(v.floatVal)));
                break;
            }
        case OpCode::BOOL_TO_INT:
            {
                auto v = pop();
                push(ConstValue::makeInt(v.boolVal ? 1 : 0));
                break;
            }
        case OpCode::INT_TO_BOOL:
            {
                auto v = pop();
                push(ConstValue::makeBool(v.intVal != 0));
                break;
            }

        case OpCode::CAST_INT:
            {
                auto v = pop();
                unsigned bits = instr.operand & 0xFFFF;
                bool sign = (instr.operand >> 16) & 1;
                push(ConstValue::makeInt(v.toInt(), bits, sign));
                break;
            }
        case OpCode::CAST_FLOAT:
            {
                auto v = pop();
                push(ConstValue::makeFloat(v.toFloat(), instr.operand));
                break;
            }

        // --- Local variables ---
        case OpCode::STORE_LOCAL:
            {
                auto val = pop();
                uint32_t absSlot = _currentLocalsBase + instr.operand;
                if (absSlot >= _config.maxLocals)
                {
                    LOG_DEBUG("[consteval] VM locals overflow (slot %u, max %u)", absSlot, _config.maxLocals);
                    return ConstValue::error();
                }
                if (absSlot >= _locals.size()) _locals.resize(absSlot + 64, ConstValue::makeInt(0));
                _locals[absSlot] = val;
                break;
            }
        case OpCode::LOAD_LOCAL:
            {
                uint32_t absSlot = _currentLocalsBase + instr.operand;
                if (absSlot >= _locals.size()) return ConstValue::error();
                push(_locals[absSlot]);
                break;
            }

        // --- Control flow ---
        case OpCode::JMP:
            ip = instr.operand;
            continue;

        case OpCode::JMP_IF_FALSE:
            {
                auto cond = pop();
                if (!cond.toBool())
                {
                    ip = instr.operand;
                    continue;
                }
                break;
            }
        case OpCode::JMP_IF_TRUE:
            {
                auto cond = pop();
                if (cond.toBool())
                {
                    ip = instr.operand;
                    continue;
                }
                break;
            }

        // --- Function calls ---
        case OpCode::CALL:
            {
                if (instr.operand >= functions.size()) return ConstValue::error();
                const CompiledFunction& func = functions[instr.operand];

                if (func.codeStart == CompiledFunction::InvalidCodeStart)
                {
                    LOG_DEBUG("[consteval] call to function '%s' that failed to compile", func.name.c_str());
                    return ConstValue::error();
                }

                if (_callFrames.size() >= _config.maxCallDepth)
                {
                    LOG_DEBUG("[consteval] VM call depth exceeded (max %u)", _config.maxCallDepth);
                    return ConstValue::error();
                }

                // Save call frame
                CallFrame frame;
                frame.returnIP = static_cast<uint32_t>(ip + 1);
                frame.localsBase = _currentLocalsBase;
                frame.numLocals = func.numLocals;
                _callFrames.push_back(frame);

                // Set up new locals base
                _currentLocalsBase = static_cast<uint32_t>(_locals.size());
                _locals.resize(_currentLocalsBase + func.numLocals, ConstValue::makeInt(0));

                // Pop args from stack into parameter slots (reverse order since stack is LIFO)
                for (int i = static_cast<int>(func.numParams) - 1; i >= 0; i--)
                {
                    _locals[_currentLocalsBase + i] = pop();
                }

                // Jump to function code
                ip = func.codeStart;
                continue;
            }

        case OpCode::RET:
            {
                ConstValue retVal = pop();

                if (_callFrames.empty())
                {
                    // Return from top-level — we're done
                    return retVal;
                }

                // Restore caller frame
                CallFrame frame = _callFrames.back();
                _callFrames.pop_back();

                // Shrink locals
                _locals.resize(frame.localsBase + frame.numLocals);
                _currentLocalsBase = frame.localsBase;

                // Push return value for caller
                push(retVal);

                ip = frame.returnIP;
                continue;
            }

        case OpCode::HALT:
            if (_stack.empty()) return ConstValue::makeVoid();
            return _stack.back();

        // --- Errors ---
        case OpCode::THROW:
            {
                // Terminates the whole evaluation (propagates through CALL frames)
                std::string name = instr.operand < stringPool.size() ? stringPool[instr.operand] : "";
                LOG_DEBUG("[consteval] VM reached throw of '%s'", name.c_str());
                return ConstValue::thrown(std::move(name));
            }
        }

        ip++;
    }

    if (!_stack.empty()) return _stack.back();
    return ConstValue::error();
}

// ============================================================
// ConstEvaluator: Public API
// ============================================================

void ConstEvaluator::syncToCompiler(BytecodeCompiler& compiler) const
{
    for (const auto& [name, val] : _constants)
        compiler.defineConstant(name, val);

    for (const auto& [name, func] : _functions)
    {
        if (func.body)
            compiler.registerFunction(name, func.paramNames, *func.body);
    }
}

ConstValue ConstEvaluator::evaluate(const Expression& expr)
{
    BytecodeCompiler compiler;
    syncToCompiler(compiler);

    // Driver code first, function bodies appended after it
    if (!compiler.compile(expr))
        return ConstValue::error();

    // Individual function compile failures mark the entry invalid; only a
    // CALL reaching such an entry turns into an evaluation error
    compiler.compileAllFunctions();

    ConstVM vm(_config);
    return vm.execute(compiler.instructions(), compiler.intPool(), compiler.int128Pool(), compiler.floatPool(),
                      compiler.stringPool(), compiler.functionTable());
}

ConstValue ConstEvaluator::evaluateFunction(const std::string& name, const std::vector<ConstValue>& args)
{
    auto it = _functions.find(name);
    if (it == _functions.end() || !it->second.body)
    {
        LOG_DEBUG("[consteval] function '%s' not registered for constexpr evaluation", name.c_str());
        return ConstValue::error();
    }

    BytecodeCompiler compiler;
    syncToCompiler(compiler);

    // Driver code first (push args, call, halt), function bodies appended after it
    if (!compiler.compileCallDriver(name, args))
        return ConstValue::error();

    compiler.compileAllFunctions();

    ConstVM vm(_config);
    return vm.execute(compiler.instructions(), compiler.intPool(), compiler.int128Pool(), compiler.floatPool(),
                      compiler.stringPool(), compiler.functionTable());
}

void ConstEvaluator::defineConstant(const std::string& name, ConstValue value)
{
    _constants[name] = value;
}

void ConstEvaluator::removeConstant(const std::string& name)
{
    _constants.erase(name);
}

void ConstEvaluator::defineFunction(const std::string& name, const FunctionDeclaration& func)
{
    if (!func.body) return;

    std::vector<std::string> paramNames;
    for (const auto& param : func.parameters)
        paramNames.push_back(param.name.token_name);
    defineFunction(name, paramNames, *func.body);
}

void ConstEvaluator::defineFunction(const std::string& name, const std::vector<std::string>& paramNames,
                                    const Block& body)
{
    ConstFunction func;
    func.paramNames = paramNames;
    func.body = &body;
    _functions[name] = func;
}

bool ConstEvaluator::hasFunction(const std::string& name) const
{
    const auto it = _functions.find(name);
    return it != _functions.end() && it->second.body != nullptr;
}

std::optional<ConstValue> ConstEvaluator::lookupConstant(const std::string& name) const
{
    auto it = _constants.find(name);
    if (it != _constants.end()) return it->second;
    return std::nullopt;
}