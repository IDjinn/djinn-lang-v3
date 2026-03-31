//
// Stack-based compile-time VM implementation — Phase 2
//

#include "ConstEvaluator.h"
#include "../utils/Logger.h"
#include <cmath>
#include <stdexcept>

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
    // Check if already registered
    auto it = _functionIndex.find(name);
    if (it != _functionIndex.end()) return it->second;

    uint32_t idx = static_cast<uint32_t>(_functionTable.size());
    _functionIndex[name] = idx;

    CompiledFunction cf;
    cf.name = name;
    cf.numParams = static_cast<uint32_t>(func.parameters.size());
    _functionTable.push_back(cf);

    return idx;
}

bool BytecodeCompiler::compileFunction(const FunctionDeclaration& func)
{
    _hadError = false;

    auto it = _functionIndex.find(func.name.token_name);
    if (it == _functionIndex.end())
    {
        _hadError = true;
        return false;
    }
    uint32_t funcIdx = it->second;

    // Save and reset local state
    auto savedLocals = std::move(_localSlots);
    uint32_t savedNextSlot = _nextLocalSlot;
    _localSlots.clear();
    _nextLocalSlot = 0;

    // Record code start
    _functionTable[funcIdx].codeStart = static_cast<uint32_t>(_instructions.size());

    // Allocate slots for parameters
    for (const auto& param : func.parameters)
    {
        getOrCreateLocal(param.name.token_name);
    }

    // Compile body
    if (func.body)
    {
        compileBlock(*func.body);
    }

    // Implicit return void if no explicit return
    emit(OpCode::PUSH_INT, addIntConstant(0));
    emit(OpCode::RET);

    _functionTable[funcIdx].numLocals = _nextLocalSlot;

    // Restore local state
    _localSlots = std::move(savedLocals);
    _nextLocalSlot = savedNextSlot;

    return !_hadError;
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
    emit(OpCode::PUSH_FLOAT, addFloatConstant(std::stod(expr.value)));
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

    switch (expr.op)
    {
    case TokenType::PLUS: emit(OpCode::ADD_INT);
        break;
    case TokenType::MINUS: emit(OpCode::SUB_INT);
        break;
    case TokenType::STAR: emit(OpCode::MUL_INT);
        break;
    case TokenType::SLASH: emit(OpCode::DIV_INT);
        break;
    case TokenType::PERCENT: emit(OpCode::MOD_INT);
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
    case TokenType::MINUS: emit(OpCode::NEG_INT);
        break;
    case TokenType::BANG: emit(OpCode::LOGIC_NOT);
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
        case OpCode::PUSH_INT: push(ConstValue::makeInt(intPool[instr.operand]));
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
                    push(ConstValue::makeInt(l.toInt() + r.toInt()));
                break;
            }
        case OpCode::SUB_INT:
            {
                auto r = pop();
                auto l = pop();
                if (l.kind == ConstValue::Float || r.kind == ConstValue::Float)
                    push(ConstValue::makeFloat(l.toFloat() - r.toFloat()));
                else
                    push(ConstValue::makeInt(l.toInt() - r.toInt()));
                break;
            }
        case OpCode::MUL_INT:
            {
                auto r = pop();
                auto l = pop();
                if (l.kind == ConstValue::Float || r.kind == ConstValue::Float)
                    push(ConstValue::makeFloat(l.toFloat() * r.toFloat()));
                else
                    push(ConstValue::makeInt(l.toInt() * r.toInt()));
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
                    int64_t ri = r.toInt();
                    if (ri == 0) return ConstValue::error();
                    push(ConstValue::makeInt(l.toInt() / ri));
                }
                break;
            }
        case OpCode::MOD_INT:
            {
                auto r = pop();
                auto l = pop();
                int64_t ri = r.toInt();
                if (ri == 0) return ConstValue::error();
                push(ConstValue::makeInt(l.toInt() % ri));
                break;
            }
        case OpCode::NEG_INT:
            {
                auto v = pop();
                if (v.kind == ConstValue::Float)
                    push(ConstValue::makeFloat(-v.floatVal, v.bitWidth));
                else
                    push(ConstValue::makeInt(-v.toInt(), v.bitWidth, v.isSigned));
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

    for (const auto& [name, funcPtr] : _functions)
        compiler.registerFunction(name, *funcPtr);
}

ConstValue ConstEvaluator::evaluate(const Expression& expr)
{
    BytecodeCompiler compiler;
    syncToCompiler(compiler);

    // Compile all registered functions first
    for (const auto& [name, funcPtr] : _functions)
        compiler.compileFunction(*funcPtr);

    if (!compiler.compile(expr))
        return ConstValue::error();

    ConstVM vm(_config);
    return vm.execute(compiler.instructions(), compiler.intPool(), compiler.int128Pool(), compiler.floatPool(),
                      compiler.functionTable());
}

ConstValue ConstEvaluator::evaluateFunction(const FunctionDeclaration& func, const std::vector<ConstValue>& args)
{
    BytecodeCompiler compiler;
    syncToCompiler(compiler);

    // Register and compile this function
    uint32_t funcIdx = compiler.registerFunction(func.name.token_name, func);

    // Compile all registered functions
    for (const auto& [name, funcPtr] : _functions)
        compiler.compileFunction(*funcPtr);
    compiler.compileFunction(func);

    // Emit: push args, call, halt
    for (const auto& arg : args)
    {
        switch (arg.kind)
        {
        case ConstValue::Integer: compiler.defineConstant("__arg", arg);
            break; // use inline push
        case ConstValue::Float: compiler.defineConstant("__arg", arg);
            break;
        case ConstValue::Bool: compiler.defineConstant("__arg", arg);
            break;
        default: return ConstValue::error();
        }
    }

    // Build a mini program: push each arg, CALL, HALT
    // We need to emit this into the instruction stream
    // Simpler approach: directly use the VM with args pre-loaded

    // Actually, let's just build call bytecode manually:
    std::vector<Instruction> callCode;
    std::vector<int64_t> intPool = {compiler.intPool().begin(), compiler.intPool().end()};
    std::vector<Int128> int128Pool = {compiler.int128Pool().begin(), compiler.int128Pool().end()};
    std::vector<double> floatPool = {compiler.floatPool().begin(), compiler.floatPool().end()};

    // Push args
    for (const auto& arg : args)
    {
        if (arg.kind == ConstValue::Integer128)
        {
            uint32_t idx = static_cast<uint32_t>(int128Pool.size());
            int128Pool.push_back(arg.int128Val);
            callCode.emplace_back(OpCode::PUSH_INT128, idx);
        }
        else if (arg.kind == ConstValue::Integer)
        {
            uint32_t idx = static_cast<uint32_t>(intPool.size());
            intPool.push_back(arg.intVal);
            callCode.emplace_back(OpCode::PUSH_INT, idx);
        }
        else if (arg.kind == ConstValue::Float)
        {
            uint32_t idx = static_cast<uint32_t>(floatPool.size());
            floatPool.push_back(arg.floatVal);
            callCode.emplace_back(OpCode::PUSH_FLOAT, idx);
        }
        else if (arg.kind == ConstValue::Bool)
        {
            callCode.emplace_back(OpCode::PUSH_BOOL, arg.boolVal ? 1u : 0u);
        }
    }
    callCode.emplace_back(OpCode::CALL, funcIdx);
    callCode.emplace_back(OpCode::HALT);

    // Prepend function bytecode from compiler
    std::vector<Instruction> fullCode;
    // Jump over function bodies to call code
    fullCode.emplace_back(OpCode::JMP, static_cast<uint32_t>(compiler.instructions().size()));
    // Append compiled function code
    for (const auto& instr : compiler.instructions())
        fullCode.push_back(instr);
    // Append call code (adjust function codeStart offsets by +1 for the JMP)
    auto funcs = compiler.functionTable();
    for (auto& f : funcs) f.codeStart += 1; // offset by the JMP instruction

    uint32_t callCodeStart = static_cast<uint32_t>(fullCode.size());
    fullCode[0].operand = callCodeStart; // patch JMP target
    for (const auto& instr : callCode)
        fullCode.push_back(instr);

    ConstVM vm(_config);
    return vm.execute(fullCode, intPool, int128Pool, floatPool, funcs);
}

void ConstEvaluator::defineConstant(const std::string& name, ConstValue value)
{
    _constants[name] = value;
}

void ConstEvaluator::defineFunction(const std::string& name, const FunctionDeclaration& func)
{
    _functions[name] = &func;
}

std::optional<ConstValue> ConstEvaluator::lookupConstant(const std::string& name) const
{
    auto it = _constants.find(name);
    if (it != _constants.end()) return it->second;
    return std::nullopt;
}