#include "../Generator.h"
#include "../ErrorTagMatching.h"
#include "../../utils/Logger.h"
#include "../../binder/ErrorTypes.h"

llvm::StructType* djinn_error_value_type(llvm::LLVMContext& context, llvm::IRBuilder<>& builder)
{
    return llvm::StructType::get(context, {builder.getInt32Ty(), builder.getPtrTy(), builder.getPtrTy()});
}

// Thread-local error state owned by the runtime (single definition shared by
// every generated module — linked libraries included), accessed field-wise.
llvm::StructType* djinn_errno_type(llvm::LLVMContext& context, llvm::IRBuilder<>& builder)
{
    return llvm::StructType::get(context, {
                                     builder.getInt32Ty(), builder.getInt32Ty(), builder.getPtrTy(), builder.getPtrTy(),
                                     builder.getPtrTy(), builder.getInt32Ty(), builder.getInt32Ty(),
                                 });
}

void Generator::ensure_error_globals_declared()
{
    if (errnoGlobal) return;

    errnoGlobal = new llvm::GlobalVariable(
        *module,
        djinn_errno_type(*context, *builder),
        false,
        llvm::GlobalVariable::ExternalLinkage,
        nullptr,
        "__djinn_errno"
    );
    errnoGlobal->setThreadLocal(true);
}

llvm::Value* Generator::errno_field(const unsigned index, const char* name)
{
    auto* ty = djinn_errno_type(*context, *builder);
    return builder->CreateStructGEP(ty, errnoGlobal, index, name);
}

llvm::Value* Generator::errno_load_i32(const unsigned index, const char* name)
{
    return builder->CreateLoad(builder->getInt32Ty(), errno_field(index, ""), true, name);
}

llvm::Value* Generator::errno_load_ptr(const unsigned index, const char* name)
{
    return builder->CreateLoad(builder->getPtrTy(), errno_field(index, ""), true, name);
}

void Generator::errno_store_i32(const unsigned index, llvm::Value* value)
{
    builder->CreateStore(value, errno_field(index, ""), true);
}

void Generator::errno_store_ptr(const unsigned index, llvm::Value* value)
{
    builder->CreateStore(value, errno_field(index, ""), true);
}

void Generator::errno_clear_flag()
{
    errno_store_i32(0, builder->getInt32(0));
}

llvm::Value* Generator::errno_load_flag(const char* name)
{
    auto* raw = errno_load_i32(0, name);
    return builder->CreateICmpNE(raw, builder->getInt32(0), std::string(name) + ".bool");
}

// Leaves the current function with an error in flight. Sync functions in the
// default mode return the default value with the error state left set
// (ordinary returns are the propagation mechanism); in native mode sync
// functions re-throw, unwinding to the caller. Async functions copy the error
// into their promise slot and go to the final suspend — unwinding cannot
// cross suspend points, and the resuming thread's error state may differ.
void Generator::emit_error_return_path()
{
    ensure_error_globals_declared();

    if (inAsyncFunction)
    {
        if (asyncErrSlotPtr)
        {
            auto* promiseTy = llvm::cast<llvm::StructType>(asyncPromiseType);
            builder->CreateStore(errno_load_i32(1, "err.slot.tag"),
                                 builder->CreateStructGEP(promiseTy, asyncErrSlotPtr, 0, "slot.tag"));
            builder->CreateStore(errno_load_ptr(2, "err.slot.msg"),
                                 builder->CreateStructGEP(promiseTy, asyncErrSlotPtr, 1, "slot.msg"));
            builder->CreateStore(errno_load_ptr(3, "err.slot.type"),
                                 builder->CreateStructGEP(promiseTy, asyncErrSlotPtr, 2, "slot.type"));
        }
        emit_all_scope_cleanup();
        if (asyncPromisePtr&& asyncReturnType &&!asyncReturnType->isVoidTy())
        {
            builder->CreateStore(get_default_value(asyncReturnType), asyncPromisePtr);
        }
        builder->CreateBr(asyncFinalSuspendBB);
        return;
    }

    emit_all_scope_cleanup();

    if (nativeExceptions)
    {
        emit_native_throw(errno_load_i32(1, "prop.tag"),
                          errno_load_ptr(2, "prop.msg"),
                          errno_load_ptr(3, "prop.type"));
        return;
    }

    llvm::Type* returnType = currentFunction ? currentFunction->getReturnType() : builder->getInt32Ty();
    if (returnType->isVoidTy())
    {
        builder->CreateRetVoid();
    }
    else
    {
        builder->CreateRet(get_default_value(returnType));
    }
}

llvm::Value* Generator::get_default_value(llvm::Type* type)
{
    if (type->isIntegerTy())
        return llvm::ConstantInt::get(type, 0);
    if (type->isFloatingPointTy())
        return llvm::ConstantFP::get(type, 0.0);
    if (type->isPointerTy())
        return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(type));
    if (type->isVoidTy())
        return nullptr;
    return llvm::Constant::getNullValue(type);
}

std::shared_ptr<StructSymbol> Generator::resolve_error_struct(const std::string& name) const
{
    if (const auto sym = symbols->lookupStruct(name))
    {
        if (sym->isErrorType) return sym;
    }
    return nullptr;
}

// Error values have the layout { i32 tag, i8* message, i8* type_name }
llvm::Value* Generator::generate_error_construction(const FunctionCall& call)
{
    ensure_error_globals_declared();

    const auto errSym = resolve_error_struct(call.name.token_name);
    if (!errSym)
    {
        throw CompileError(DiagnosticCode::UNDEFINED_STRUCT, "unknown error type: " + call.name.token_name);
    }

    auto* errType = djinn_error_value_type(*context, *builder);
    auto* alloca = builder->CreateAlloca(errType, nullptr, "err_val");

    auto* tagPtr = builder->CreateStructGEP(errType, alloca, 0, "err_tag_ptr");
    builder->CreateStore(builder->getInt32(errSym->errorTag), tagPtr);

    llvm::Value* msgPtr = llvm::ConstantPointerNull::get(builder->getPtrTy());
    if (call.arguments.size() > 1)
    {
        // Interpolated message: the parser desugared "msg {expr}" into
        // ("msg {0}", expr) — format it at runtime
        msgPtr = coerce_str_to_ptr(generate_interpolated_error_message(call));
    }
    else if (!call.arguments.empty())
    {
        auto* msgVal = generate_expression(*call.arguments[0]);
        msgPtr = coerce_str_to_ptr(msgVal);
    }

    auto* msgFieldPtr = builder->CreateStructGEP(errType, alloca, 1, "err_msg_ptr");
    builder->CreateStore(msgPtr, msgFieldPtr);

    // Type name for uncaught-exception reports; one deduped global per error
    // type, and displayed without the namespace qualification
    auto name = errSym->name;
    if (const auto pos = name.rfind("::"); pos != std::string::npos)
        name = name.substr(pos + 2);
    auto* nameFieldPtr = builder->CreateStructGEP(errType, alloca, 2, "err_type_ptr");
    builder->CreateStore(cached_global_string(name, "err.type"), nameFieldPtr);

    return alloca;
}

// Formats an interpolated error message ("msg {expr}") into the runtime's
// fixed thread-local buffer (__djinn_error_format) — allocation-free, and the
// buffer outlives unwinding so the message pointer stays valid in both error
// modes. Args are boxed exactly like Console.format varargs.
llvm::Value* Generator::generate_interpolated_error_message(const FunctionCall& call)
{
    auto* fmtTy = llvm::FunctionType::get(
        builder->getPtrTy(),
        {builder->getPtrTy(), builder->getInt32Ty(), builder->getPtrTy(), builder->getInt32Ty()},
        false);
    auto* fmtFn = module->getFunction("__djinn_error_format");
    if (!fmtFn)
    {
        fmtFn = llvm::Function::Create(fmtTy, llvm::Function::ExternalLinkage,
                                       "__djinn_error_format", *module);
    }

    auto* fmtVal = generate_expression(*call.arguments[0]);
    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(fmtVal))
    {
        if (llvm::isa<llvm::StructType>(alloca->getAllocatedType()))
            fmtVal = builder->CreateLoad(alloca->getAllocatedType(), fmtVal, "fmt_load");
    }
    else if (fmtVal->getType()->isPointerTy())
    {
        fmtVal = builder->CreateLoad(
            llvm::StructType::get(builder->getPtrTy(), builder->getInt32Ty()), fmtVal, "fmt_load");
    }

    llvm::Value* fmtData = builder->CreateExtractValue(fmtVal, 0, "fmt.data");
    llvm::Value* fmtLen = builder->CreateExtractValue(fmtVal, 1, "fmt.len");

    llvm::Value* varargs = emit_boxed_varargs_array(call.arguments, 1);
    auto* objData = builder->CreateExtractValue(varargs, 0, "args.data");
    auto* objCount = builder->CreateExtractValue(varargs, 1, "args.len");

    return builder->CreateCall(fmtFn, {fmtData, fmtLen, objData, objCount}, "err_fmt");
}

void Generator::generate_throw_statement(const ThrowStatement& stmt)
{
    ensure_error_globals_declared();

    llvm::Value* tagVal = builder->getInt32(0);
    llvm::Value* msgVal = llvm::ConstantPointerNull::get(builder->getPtrTy());
    llvm::Value* nameVal = llvm::ConstantPointerNull::get(builder->getPtrTy());

    if (stmt.expression)
    {
        llvm::Value* errPtr = generate_expression(*stmt.expression);

        if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(errPtr))
        {
            errPtr = alloca;
        }
        else if (!errPtr->getType()->isPointerTy())
        {
            // Plain struct value: spill it so we can read the tag
            auto* errType = djinn_error_value_type(*context, *builder);
            auto* tmp = builder->CreateAlloca(errType, nullptr, "throw_tmp");
            builder->CreateStore(errPtr, tmp);
            errPtr = tmp;
        }

        llvm::StructType* errType = nullptr;
        if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(errPtr))
            errType = llvm::cast<llvm::StructType>(alloca->getAllocatedType());
        else
            errType = djinn_error_value_type(*context, *builder);

        if (errType->getNumElements() >= 1)
        {
            auto* tagPtr = builder->CreateStructGEP(errType, errPtr, 0, "throw_tag_ptr");
            tagVal = builder->CreateLoad(builder->getInt32Ty(), tagPtr, "throw_tag");
        }

        if (errType->getNumElements() >= 2)
        {
            auto* msgPtr = builder->CreateStructGEP(errType, errPtr, 1, "throw_msg_ptr");
            msgVal = builder->CreateLoad(builder->getPtrTy(), msgPtr, "throw_msg");
        }

        if (errType->getNumElements() >= 3)
        {
            auto* namePtr = builder->CreateStructGEP(errType, errPtr, 2, "throw_type_ptr");
            nameVal = builder->CreateLoad(builder->getPtrTy(), namePtr, "throw_type");
        }
    }

    store_error_origin(stmt.location);

    if (nativeExceptions)
    {
        // The shim mirrors the error into the thread-local state before the
        // throw, keeping report rendering uniform across both modes
        errno_store_i32(1, tagVal);
        errno_store_ptr(2, msgVal);
        errno_store_ptr(3, nameVal);
        emit_native_throw(tagVal, msgVal, nameVal);
        return;
    }

    emit_error_trace_capture();

    errno_store_i32(0, builder->getInt32(1));
    errno_store_i32(1, tagVal);
    errno_store_ptr(2, msgVal);
    errno_store_ptr(3, nameVal);

    emit_error_return_path();
}

// After a call to a throwing function inside another throwing function
// (unchecked call sites), re-throw when the callee failed. Native mode
// propagates by unwinding instead: the call itself is an invoke and this
// check is not emitted.
void Generator::emit_error_propagation_check(const SourceLocation& loc)
{
    if (nativeExceptions) return;

    ensure_error_globals_declared();

    auto* errorFlag = errno_load_flag("prop_err_flag");

    auto* llvmFunc = builder->GetInsertBlock()->getParent();
    auto* contBB = llvm::BasicBlock::Create(*context, "prop.ok", llvmFunc);
    auto* errBB = llvm::BasicBlock::Create(*context, "prop.err", llvmFunc);

    builder->CreateCondBr(errorFlag, errBB, contBB);

    builder->SetInsertPoint(errBB);
    store_error_origin(loc);
    emit_error_return_path();

    builder->SetInsertPoint(contBB);
}

// Reports the error currently held in the error state and aborts: loads
// tag/type/message/origin and tail-calls __djinn_uncaught_error (noreturn).
void Generator::emit_uncaught_error_trap()
{
    ensure_error_globals_declared();

    auto* uncaughtFn = module->getFunction("__djinn_uncaught_error");
    if (!uncaughtFn)
    {
        auto* uncaughtTy = llvm::FunctionType::get(builder->getVoidTy(),
                                                   {
                                                       builder->getInt32Ty(), builder->getPtrTy(),
                                                       builder->getPtrTy(), builder->getPtrTy(),
                                                       builder->getInt32Ty(), builder->getInt32Ty(),
                                                   }, false);
        uncaughtFn = llvm::Function::Create(uncaughtTy, llvm::Function::ExternalLinkage,
                                            "__djinn_uncaught_error", *module);
    }

    auto* tag = errno_load_i32(1, "uncaught_tag");
    auto* name = errno_load_ptr(3, "uncaught_type");
    auto* payload = errno_load_ptr(2, "uncaught_msg");
    auto* originFile = errno_load_ptr(4, "uncaught_file");
    auto* originLine = errno_load_i32(5, "uncaught_line");
    auto* originCol = errno_load_i32(6, "uncaught_col");
    builder->CreateCall(uncaughtFn, {tag, name, payload, originFile, originLine, originCol});
    builder->CreateUnreachable();
}

// After user main returns: an error flag still set means an exception
// escaped main() throws — report it and abort instead of exiting silently.
void Generator::emit_uncaught_error_check()
{
    ensure_error_globals_declared();

    auto* errorFlag = errno_load_flag("uncaught_flag");

    auto* llvmFunc = builder->GetInsertBlock()->getParent();
    auto* okBB = llvm::BasicBlock::Create(*context, "main.err.ok", llvmFunc);
    auto* errBB = llvm::BasicBlock::Create(*context, "main.err.uncaught", llvmFunc);

    builder->CreateCondBr(errorFlag, errBB, okBB);

    builder->SetInsertPoint(errBB);
    emit_uncaught_error_trap();

    builder->SetInsertPoint(okBB);
}

// Division/remission by zero: throws DivisionByZero in throwing functions,
// traps via __djinn_runtime_error otherwise.
void Generator::emit_div_by_zero_check(const TrapOperand& dividend, const TrapOperand& divisor,
                                       const SourceLocation& loc)
{
    ensure_error_globals_declared();

    auto* isZero = builder->CreateICmpEQ(divisor.value,
                                         llvm::ConstantInt::get(divisor.value->getType(), 0), "div_zero");

    auto* llvmFunc = builder->GetInsertBlock()->getParent();
    auto* okBB = llvm::BasicBlock::Create(*context, "div.ok", llvmFunc);
    auto* errBB = llvm::BasicBlock::Create(*context, "div.zero", llvmFunc);

    builder->CreateCondBr(isZero, errBB, okBB);

    builder->SetInsertPoint(errBB);

    if (currentFunctionThrows)
    {
        emit_error_throw_with_tag(djinn::errors::builtin_error_tag("DivisionByZero"), loc);
    }
    else
    {
        // Trap with a rich report so non-throwing code fails loudly instead of UB
        emit_runtime_error_trap(loc, "division by zero", '/', dividend, divisor, true);
    }

    builder->SetInsertPoint(okBB);
}

// Set up contract state for the function being generated: binds the `return`
// pseudo-variable for ensure clauses and emits require checks at entry. Only
// `in mode check` clauses reach the binary (spec §5.2) — assume/prove/ignore
// are compile-time dispositions and emit nothing.
void Generator::setup_contracts(const std::vector<const ContractClause*>& contracts, llvm::Function* llvmFunc)
{
    currentContracts_ = contracts;
    contractReturnAlloca = nullptr;
    oldSnapshots_.clear();

    bool hasEnsure = false;
    for (const auto& contract : contracts)
    {
        if (contract && contract->isEnsure() && contract->condition &&
            contract->mode == ContractClause::Mode::Check)
        {
            hasEnsure = true;
            break;
        }
    }

    if (hasEnsure && llvmFunc && !llvmFunc->getReturnType()->isVoidTy())
    {
        contractReturnAlloca = builder->CreateAlloca(llvmFunc->getReturnType(), nullptr, "__return");
        currentScope->define_variable("return", contractReturnAlloca, "");
    }

    // Runtime `old()`: every field path under old() in a checked claim is
    // captured at entry; ensure checks read the snapshot at return time and
    // requires read it at entry (where snapshot and current value coincide).
    std::vector<const Expression*> oldPaths;
    for (const auto& contract : contracts)
    {
        if (!contract || !contract->condition) continue;
        if (contract->mode != ContractClause::Mode::Check) continue;
        collect_old_paths(*contract->condition, oldPaths);
    }
    for (const auto* path : oldPaths)
    {
        auto* value = generate_expression(*path);
        auto* snapshot = builder->CreateAlloca(value->getType(), nullptr, "__old");
        builder->CreateStore(value, snapshot);
        oldSnapshots_[old_path_key(*path)] = snapshot;
    }

    emit_contract_requirements();
}

// "name.field" for the field paths old() accepts (plus the bare "name" form
// for scalars), empty otherwise.
std::string Generator::old_path_key(const Expression& expr) const
{
    if (const auto* ident = dynamic_cast<const Identifier*>(&expr))
        return ident->identifier.token_name;
    const auto* field = dynamic_cast<const FieldAccess*>(&expr);
    if (!field) return "";
    const auto* object = dynamic_cast<const Identifier*>(field->object.get());
    if (!object) return "";
    return object->name() + "." + field->fieldName.token_name;
}

void Generator::collect_old_paths(const Expression& expr, std::vector<const Expression*>& out) const
{
    if (const auto* call = dynamic_cast<const FunctionCall*>(&expr))
    {
        if (call->name.token_name == "old" && call->arguments.size() == 1 && !call->receiver)
        {
            if (!old_path_key(*call->arguments.front()).empty())
                out.push_back(call->arguments.front().get());
            return;
        }
        for (const auto& argument : call->arguments)
            collect_old_paths(*argument, out);
        if (call->receiver) collect_old_paths(*call->receiver, out);
        return;
    }
    if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expr))
    {
        collect_old_paths(*binary->left, out);
        collect_old_paths(*binary->right, out);
        return;
    }
    if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expr))
    {
        collect_old_paths(*unary->operand, out);
        return;
    }
    if (const auto* postfix = dynamic_cast<const PostfixExpression*>(&expr))
    {
        collect_old_paths(*postfix->operand, out);
        return;
    }
    if (const auto* field = dynamic_cast<const FieldAccess*>(&expr))
    {
        collect_old_paths(*field->object, out);
        return;
    }
    if (const auto* fieldAssign = dynamic_cast<const FieldAssignment*>(&expr))
    {
        collect_old_paths(*fieldAssign->object, out);
        collect_old_paths(*fieldAssign->value, out);
        return;
    }
    if (const auto* index = dynamic_cast<const IndexAccess*>(&expr))
    {
        collect_old_paths(*index->object, out);
        collect_old_paths(*index->index, out);
        return;
    }
    if (const auto* indexAssign = dynamic_cast<const IndexAssignment*>(&expr))
    {
        collect_old_paths(*indexAssign->object, out);
        collect_old_paths(*indexAssign->index, out);
        collect_old_paths(*indexAssign->value, out);
        return;
    }
    if (const auto* init = dynamic_cast<const VariableInit*>(&expr))
    {
        collect_old_paths(*init->value, out);
        return;
    }
    if (const auto* assign = dynamic_cast<const Assignment*>(&expr))
    {
        collect_old_paths(*assign->value, out);
        return;
    }
    if (const auto* switchExpr = dynamic_cast<const SwitchExpression*>(&expr))
    {
        collect_old_paths(*switchExpr->value, out);
        for (const auto& arm : switchExpr->arms)
        {
            if (arm.result) collect_old_paths(*arm.result, out);
            if (arm.block) collect_old_paths_in_block(*arm.block, out);
        }
        return;
    }
    if (const auto* arrayLiteral = dynamic_cast<const ArrayLiteral*>(&expr))
    {
        for (const auto& element : arrayLiteral->elements)
            collect_old_paths(*element, out);
        return;
    }
}

// Statement walk gathering old() uses from a whole member body so their
// entry snapshots exist before any statement runs.
void Generator::collect_old_paths_in_block(const Block& block, std::vector<const Expression*>& out) const
{
    for (const auto& stmt : block.statements)
    {
        if (!stmt) continue;
        if (const auto* exprStmt = dynamic_cast<const ExpressionStatement*>(stmt.get()))
        {
            collect_old_paths(*exprStmt->expression, out);
        }
        else if (const auto* ret = dynamic_cast<const ReturnStatement*>(stmt.get()))
        {
            if (ret->value) collect_old_paths(*ret->value, out);
        }
        else if (dynamic_cast<const CommitStatement*>(stmt.get()) ||
                 dynamic_cast<const RollbackStatement*>(stmt.get()) ||
                 dynamic_cast<const BreakStatement*>(stmt.get()) ||
                 dynamic_cast<const ContinueStatement*>(stmt.get()))
        {
            continue;
        }
        else if (const auto* nested = dynamic_cast<const Block*>(stmt.get()))
        {
            collect_old_paths_in_block(*nested, out);
        }
        else if (const auto* ifStmt = dynamic_cast<const IfStatement*>(stmt.get()))
        {
            if (ifStmt->condition) collect_old_paths(*ifStmt->condition, out);
            if (ifStmt->thenBranch) collect_old_paths_in_block(*ifStmt->thenBranch, out);
            if (ifStmt->elseBranch) collect_old_paths_in_block(*ifStmt->elseBranch, out);
        }
        else if (const auto* forStmt = dynamic_cast<const ForStatement*>(stmt.get()))
        {
            if (forStmt->initializer) collect_old_paths(*forStmt->initializer, out);
            if (forStmt->condition) collect_old_paths(*forStmt->condition, out);
            if (forStmt->postfix) collect_old_paths(*forStmt->postfix, out);
            if (forStmt->body) collect_old_paths_in_block(*forStmt->body, out);
        }
        else if (const auto* rangeFor = dynamic_cast<const RangeForStatement*>(stmt.get()))
        {
            if (rangeFor->start) collect_old_paths(*rangeFor->start, out);
            if (rangeFor->end) collect_old_paths(*rangeFor->end, out);
            if (rangeFor->body) collect_old_paths_in_block(*rangeFor->body, out);
        }
        else if (const auto* whileStmt = dynamic_cast<const WhileStatement*>(stmt.get()))
        {
            if (whileStmt->condition) collect_old_paths(*whileStmt->condition, out);
            if (whileStmt->body) collect_old_paths_in_block(*whileStmt->body, out);
        }
        else if (const auto* doWhile = dynamic_cast<const DoWhileStatement*>(stmt.get()))
        {
            if (doWhile->body) collect_old_paths_in_block(*doWhile->body, out);
            if (doWhile->condition) collect_old_paths(*doWhile->condition, out);
        }
        else if (const auto* yield = dynamic_cast<const YieldStatement*>(stmt.get()))
        {
            if (yield->value) collect_old_paths(*yield->value, out);
        }
        else if (const auto* spawn = dynamic_cast<const SpawnStatement*>(stmt.get()))
        {
            if (spawn->expression) collect_old_paths(*spawn->expression, out);
        }
        else if (const auto* throwStmt = dynamic_cast<const ThrowStatement*>(stmt.get()))
        {
            if (throwStmt->expression) collect_old_paths(*throwStmt->expression, out);
        }
        else if (const auto* switchStmt = dynamic_cast<const SwitchStatement*>(stmt.get()))
        {
            if (switchStmt->value) collect_old_paths(*switchStmt->value, out);
            for (const auto& caseStmt : switchStmt->cases)
            {
                if (caseStmt->expression) collect_old_paths(*caseStmt->expression, out);
                if (caseStmt->body) collect_old_paths_in_block(*caseStmt->body, out);
            }
        }
        else if (const auto* lock = dynamic_cast<const LockStatement*>(stmt.get()))
        {
            if (lock->body) collect_old_paths_in_block(*lock->body, out);
        }
        else if (const auto* tryCatch = dynamic_cast<const TryCatchStatement*>(stmt.get()))
        {
            if (tryCatch->tryBlock) collect_old_paths_in_block(*tryCatch->tryBlock, out);
            for (const auto& clause : tryCatch->catches)
            {
                if (clause.body) collect_old_paths_in_block(*clause.body, out);
            }
            if (tryCatch->finallyBlock) collect_old_paths_in_block(*tryCatch->finallyBlock, out);
        }
        else if (const auto* phase = dynamic_cast<const UowPhaseBlockStatement*>(stmt.get()))
        {
            if (phase->body) collect_old_paths_in_block(*phase->body, out);
        }
    }
}

// `old()` in member code — transaction bodies (including after `commit;`) and
// catch/finally arms — reads the same entry snapshots the checked claims use,
// so pre-state stays observable for the whole member lifetime.
void Generator::setup_body_old_snapshots(const Block* body,
                                         const std::vector<const CatchClause*>& catchArms,
                                         const Block* finallyBlock)
{
    std::vector<const Expression*> oldPaths;
    if (body) collect_old_paths_in_block(*body, oldPaths);
    for (const auto* arm : catchArms)
    {
        if (arm && arm->body) collect_old_paths_in_block(*arm->body, oldPaths);
    }
    if (finallyBlock) collect_old_paths_in_block(*finallyBlock, oldPaths);

    for (const auto* path : oldPaths)
    {
        const std::string key = old_path_key(*path);
        if (key.empty() || oldSnapshots_.contains(key)) continue;
        auto* value = generate_expression(*path);
        auto* snapshot = builder->CreateAlloca(value->getType(), nullptr, "__old");
        builder->CreateStore(value, snapshot);
        oldSnapshots_[key] = snapshot;
    }
}

// Non-zero parameter entry check: the parameter type is an implicit
// require(param != 0) clause. Throws ContractViolation when violated, which
// is what lets division-by-zero checks inside the body be elided.
void Generator::emit_non_zero_param_check(const std::string& paramName, const Type& paramType)
{
    if (paramType.kind != TypeKind::INTEGER || !paramType.nonZero) return;

    auto* alloca = currentScope->lookup_variable(paramName);
    if (!alloca) return;

    auto* value = builder->CreateLoad(alloca->getAllocatedType(), alloca, paramName + ".nz_load");
    auto* isZero = builder->CreateICmpEQ(
        value, llvm::ConstantInt::get(value->getType(), 0), "nz_zero");

    auto* llvmFunc = builder->GetInsertBlock()->getParent();
    auto* okBB = llvm::BasicBlock::Create(*context, "nz.ok", llvmFunc);
    auto* violBB = llvm::BasicBlock::Create(*context, "nz.violation", llvmFunc);

    builder->CreateCondBr(isZero, violBB, okBB);

    builder->SetInsertPoint(violBB);
    emit_error_throw_with_tag(djinn::errors::builtin_error_tag("ContractViolation"), paramType.location);

    builder->SetInsertPoint(okBB);
}

// Require checks: evaluate each precondition at function entry and throw
// ContractViolation when one fails. A require that merely restates a non-zero
// guarantee (declared i32n or upgraded from require(p != 0)) is skipped —
// the non-zero entry check already covers it.
void Generator::emit_contract_requirements()
{
    for (const auto& contract : currentContracts_)
    {
        if (!contract || !contract->isRequire() || !contract->condition) continue;
        if (contract->mode != ContractClause::Mode::Check) continue;

        if (const auto proven = non_zero_proven_identifier(*contract->condition);
            proven && currentScope->lookup_variable_non_zero(*proven).value_or(false))
        {
            continue;
        }

        auto* condVal = generate_expression(*contract->condition);
        if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(condVal))
        {
            condVal = builder->CreateLoad(alloca->getAllocatedType(), alloca, "req_cond_load");
        }
        auto* condTrue = builder->CreateICmpNE(
            condVal, llvm::ConstantInt::get(condVal->getType(), 0), "req_cond");

        auto* llvmFunc = builder->GetInsertBlock()->getParent();
        auto* okBB = llvm::BasicBlock::Create(*context, "req.ok", llvmFunc);
        auto* violBB = llvm::BasicBlock::Create(*context, "req.violation", llvmFunc);

        builder->CreateCondBr(condTrue, okBB, violBB);

        builder->SetInsertPoint(violBB);
        emit_error_throw_with_tag(djinn::errors::builtin_error_tag("ContractViolation"),
                                  contract->condition->location);

        builder->SetInsertPoint(okBB);
    }
}

// Ensure checks: called right before each return statement (the return value
// has already been stored in the `return` pseudo-variable).
void Generator::emit_contract_ensures()
{
    for (const auto& contract : currentContracts_)
    {
        if (!contract || !contract->isEnsure() || !contract->condition) continue;
        if (contract->mode != ContractClause::Mode::Check) continue;

        auto* condVal = generate_expression(*contract->condition);
        if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(condVal))
        {
            condVal = builder->CreateLoad(alloca->getAllocatedType(), alloca, "ens_cond_load");
        }
        auto* condTrue = builder->CreateICmpNE(
            condVal, llvm::ConstantInt::get(condVal->getType(), 0), "ens_cond");

        auto* llvmFunc = builder->GetInsertBlock()->getParent();
        auto* okBB = llvm::BasicBlock::Create(*context, "ens.ok", llvmFunc);
        auto* violBB = llvm::BasicBlock::Create(*context, "ens.violation", llvmFunc);

        builder->CreateCondBr(condTrue, okBB, violBB);

        builder->SetInsertPoint(violBB);
        emit_error_throw_with_tag(djinn::errors::builtin_error_tag("ContractViolation"),
                                  contract->condition->location);

        builder->SetInsertPoint(okBB);
    }
}

// Records where an error was raised so uncaught-exception reports can point
// at the failing expression even without a stack trace (release builds).
void Generator::store_error_origin(const SourceLocation& loc)
{
    llvm::Value* filePtr = llvm::ConstantPointerNull::get(builder->getPtrTy());
    if (!loc.fileId.empty())
        filePtr = cached_global_string(loc.fileId, "err.origin.file");
    errno_store_ptr(4, filePtr);
    errno_store_i32(5, builder->getInt32(loc.line));
    errno_store_i32(6, builder->getInt32(loc.column));
}

// Marks the error flag/tag and leaves the function with the error in flight
// (used by throw and contract violations).
void Generator::emit_error_throw_with_tag(const int32_t tag, const SourceLocation& loc)
{
    ensure_error_globals_declared();

    store_error_origin(loc);

    if (nativeExceptions)
    {
        errno_store_i32(0, builder->getInt32(1));
        errno_store_i32(1, builder->getInt32(tag));
        // Builtin-tag errors carry no type name of their own — clear any name left
        // by a previous user exception so reports fall back to the builtin table
        errno_store_ptr(3, llvm::ConstantPointerNull::get(builder->getPtrTy()));
        emit_native_throw(builder->getInt32(tag),
                          llvm::ConstantPointerNull::get(builder->getPtrTy()),
                          llvm::ConstantPointerNull::get(builder->getPtrTy()));
        return;
    }

    errno_store_i32(1, builder->getInt32(tag));
    // Builtin-tag errors carry no type name of their own — clear any name left
    // by a previous user exception so reports fall back to the builtin table
    errno_store_ptr(3, llvm::ConstantPointerNull::get(builder->getPtrTy()));
    errno_store_i32(0, builder->getInt32(1));
    emit_error_trace_capture();

    emit_error_return_path();
}

// Shared guarded-region lowering: a body running under one landing, catch
// arms matched by error tag in source order, optional finally on every
// non-unwinding path plus once before a re-throw. Used by the try/catch
// statement and the function-level handler suffix.
void Generator::generate_guarded_region(const SourceLocation& loc,
                                        const std::function<void()>& generateBody,
                                        const std::vector<const CatchClause*>& arms,
                                        const Block* finallyBlock)
{
    if (!nativeExceptions)
    {
        GENERATOR_ERROR(DiagnosticCode::TRY_CATCH_REQUIRES_EXCEPTIONS,
                        "catch handlers require the exceptions mode ('--exceptions')",
                        loc);
    }
    if (!eh_is_msvc_target())
    {
        GENERATOR_ERROR(DiagnosticCode::TRY_CATCH_REQUIRES_EXCEPTIONS,
                        "native exceptions are not supported on this target yet",
                        loc);
    }

    push_scope();

    auto* func = builder->GetInsertBlock()->getParent();
    auto* dispatchBB = llvm::BasicBlock::Create(*context, "guard.dispatch", func);
    auto* contBB = llvm::BasicBlock::Create(*context, "guard.cont", func);
    auto* rethrowBB = llvm::BasicBlock::Create(*context, "guard.rethrow", func);
    auto* finallyBB = finallyBlock
                          ? llvm::BasicBlock::Create(*context, "guard.finally", func)
                          : contBB;

    const auto landing = push_native_landing(false);

    const bool prevInsideTry = insideTryOperand_;
    insideTryOperand_ = true;
    generateBody();
    insideTryOperand_ = prevInsideTry;
    ehLandingStack_.pop_back();

    // Normal completion of the guarded body skips the dispatch entirely — the
    // dispatcher is only for unwinding pads, and dispatching an unset error
    // flag would rethrow garbage
    if (!builder->GetInsertBlock()->getTerminator())
    {
        builder->CreateBr(finallyBB);
    }

    // Arms match by tag in source order (specific types match derived errors
    // too; Error/_ catch everything) — same semantics as outcome-switch arms
    builder->SetInsertPoint(dispatchBB);
    auto* thrownTag = errno_load_i32(1, "guard.tag");

    for (size_t i = 0; i < arms.size(); ++i)
    {
        const auto* clause = arms[i];
        auto* armBB = llvm::BasicBlock::Create(*context, "guard.arm." + clause->errorType.token_name, func);
        llvm::BasicBlock* nextArmBB = i + 1 < arms.size()
                                          ? llvm::BasicBlock::Create(*context, "guard.next", func)
                                          : rethrowBB;

        const auto errSym = resolve_error_struct(clause->errorType.token_name);
        if (errSym)
        {
            llvm::Value* matched = nullptr;
            for (const int32_t tag : djinn::error_arm_matched_tags(*symbols, *errSym))
            {
                auto* cmp = builder->CreateICmpEQ(thrownTag, builder->getInt32(tag), "guard.cmp");
                matched = matched ? builder->CreateOr(matched, cmp) : cmp;
            }
            builder->CreateCondBr(matched, armBB, nextArmBB);
        }
        else
        {
            // "Error" or "_" — catch-all
            builder->CreateBr(armBB);
        }

        builder->SetInsertPoint(armBB);
        errno_clear_flag();

        push_scope();
        if (clause->binding)
        {
            auto* errType = djinn_error_value_type(*context, *builder);
            auto* errAlloca = builder->CreateAlloca(errType, nullptr, clause->binding->token_name);
            builder->CreateStore(thrownTag,
                                 builder->CreateStructGEP(errType, errAlloca, 0, "bind.tag"));
            builder->CreateStore(errno_load_ptr(2, "bind.msg"),
                                 builder->CreateStructGEP(errType, errAlloca, 1, "bind.msg.ptr"));
            builder->CreateStore(errno_load_ptr(3, "bind.type"),
                                 builder->CreateStructGEP(errType, errAlloca, 2, "bind.type.ptr"));
            currentScope->define_variable(clause->binding->token_name, errAlloca);
        }
        generate_block(*clause->body);
        pop_scope();

        if (!builder->GetInsertBlock()->getTerminator())
        {
            builder->CreateBr(finallyBB);
        }

        if (i + 1 < arms.size())
        {
            builder->SetInsertPoint(nextArmBB);
        }
    }

    // No arm matched (or no catch arms at all — finally-only guard): run
    // finally once more, then re-throw
    if (arms.empty())
    {
        builder->SetInsertPoint(dispatchBB);
        builder->CreateBr(rethrowBB);
    }
    builder->SetInsertPoint(rethrowBB);
    if (finallyBlock)
    {
        push_scope();
        generate_block(*finallyBlock);
        pop_scope();
    }
    ensure_error_globals_declared();
    emit_native_throw(errno_load_i32(1, "rethrow.tag"),
                      errno_load_ptr(2, "rethrow.msg"),
                      errno_load_ptr(3, "rethrow.type"));

    if (finallyBlock)
    {
        builder->SetInsertPoint(finallyBB);
        push_scope();
        generate_block(*finallyBlock);
        pop_scope();
        if (!builder->GetInsertBlock()->getTerminator())
        {
            builder->CreateBr(contBB);
        }
    }

    // Fill the landing blocks now that the dispatch exists
    finalize_native_landing(landing, dispatchBB);

    builder->SetInsertPoint(contBB);
    pop_scope();
}

// Block-form try/catch/finally (native mode only). The try body runs with a
// landing whose pads catchret to a normal dispatch block, so handler bodies,
// return/break/continue and nested tries all behave like ordinary code.
void Generator::generate_try_catch_statement(const TryCatchStatement& stmt)
{
    std::vector<const CatchClause*> arms;
    for (const auto& clause : stmt.catches)
    {
        arms.push_back(&clause);
    }

    generate_guarded_region(stmt.location,
                            [&] { generate_block(*stmt.tryBlock); },
                            arms, stmt.finallyBlock.get());
}

// After a child coroutine completes: transfers its promise error slot (the
// values were loaded before the frame was destroyed) into the thread-local
// error state and propagates. Inside a try/switch operand the flag is left
// set for the operand check instead — same rule as throwing calls.
void Generator::emit_await_error_check(llvm::Value* errTag, llvm::Value* errMsg, llvm::Value* errType,
                                       const SourceLocation& loc)
{
    ensure_error_globals_declared();

    auto* hasError = builder->CreateICmpNE(errTag, builder->getInt32(0), "await.haserr");

    auto* llvmFunc = builder->GetInsertBlock()->getParent();
    auto* okBB = llvm::BasicBlock::Create(*context, "await.ok", llvmFunc);
    auto* errBB = llvm::BasicBlock::Create(*context, "await.err", llvmFunc);

    builder->CreateCondBr(hasError, errBB, okBB);

    builder->SetInsertPoint(errBB);
    errno_store_i32(0, builder->getInt32(1));
    errno_store_i32(1, errTag);
    errno_store_ptr(2, errMsg);
    errno_store_ptr(3, errType);

    if (insideTryOperand_)
    {
        builder->CreateBr(okBB);
    }
    else
    {
        store_error_origin(loc);
        emit_error_return_path();
    }

    builder->SetInsertPoint(okBB);
}

llvm::Value* Generator::generate_ternary_expression(const TernaryExpression& expr)
{
    auto* condVal = generate_expression(*expr.condition);

    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(condVal))
    {
        condVal = builder->CreateLoad(alloca->getAllocatedType(), alloca, "ternary_cond_load");
    }

    condVal = builder->CreateICmpNE(
        condVal,
        llvm::ConstantInt::get(condVal->getType(), 0),
        "ternary_cond"
    );

    auto* llvmFunc = builder->GetInsertBlock()->getParent();
    auto* trueBB = llvm::BasicBlock::Create(*context, "ternary.true", llvmFunc);
    auto* falseBB = llvm::BasicBlock::Create(*context, "ternary.false", llvmFunc);
    auto* mergeBB = llvm::BasicBlock::Create(*context, "ternary.merge", llvmFunc);

    builder->CreateCondBr(condVal, trueBB, falseBB);

    builder->SetInsertPoint(trueBB);
    auto* trueVal = generate_expression(*expr.trueExpr);
    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(trueVal))
    {
        trueVal = builder->CreateLoad(alloca->getAllocatedType(), alloca, "ternary_true_load");
    }
    builder->CreateBr(mergeBB);

    builder->SetInsertPoint(falseBB);
    auto* falseVal = generate_expression(*expr.falseExpr);
    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(falseVal))
    {
        falseVal = builder->CreateLoad(alloca->getAllocatedType(), alloca, "ternary_false_load");
    }
    builder->CreateBr(mergeBB);

    builder->SetInsertPoint(mergeBB);
    auto* phi = builder->CreatePHI(trueVal->getType(), 2, "ternary_result");
    phi->addIncoming(trueVal, trueBB);
    phi->addIncoming(falseVal, falseBB);
    return phi;
}

llvm::Value* Generator::generate_try_expression(const TryExpression& expr)
{
    ensure_error_globals_declared();

    errno_clear_flag();

    // Native mode: the operand's throwing calls unwind to the landing's
    // pads, which resume here — at a dedicated, side-effect-free check block
    // — with the error state set by the shim.
    NativeLanding landing;
    const bool nativeLanding = nativeExceptions;
    if (nativeLanding)
    {
        landing = push_native_landing(false);
    }

    const bool prevInsideTry = insideTryOperand_;
    insideTryOperand_ = true;
    auto* callResult = generate_expression(*expr.expr);
    insideTryOperand_ = prevInsideTry;

    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(callResult))
    {
        callResult = builder->CreateLoad(alloca->getAllocatedType(), alloca, "try_load");
    }

    llvm::Value* errorFlag = nullptr;
    if (nativeLanding)
    {
        auto* llvmFunc = builder->GetInsertBlock()->getParent();
        auto* checkBB = llvm::BasicBlock::Create(*context, "try.check", llvmFunc);
        builder->CreateBr(checkBB);
        builder->SetInsertPoint(checkBB);
        errorFlag = errno_load_flag("try_err_flag");
        finalize_native_landing(landing, checkBB);
        ehLandingStack_.pop_back();
    }
    else
    {
        errorFlag = errno_load_flag("try_err_flag");
    }

    if (!expr.fallback)
    {
        if (currentFunctionThrows)
        {
            // No fallback: propagate the error to this function's callers,
            // with the origin rising to this unhandled try call site
            auto* llvmFunc = builder->GetInsertBlock()->getParent();
            auto* okBB = llvm::BasicBlock::Create(*context, "try.prop.ok", llvmFunc);
            auto* errBB = llvm::BasicBlock::Create(*context, "try.prop.err", llvmFunc);

            builder->CreateCondBr(errorFlag, errBB, okBB);

            builder->SetInsertPoint(errBB);
            store_error_origin(expr.location);
            emit_error_return_path();

            builder->SetInsertPoint(okBB);
        }
        return callResult;
    }

    auto* llvmFunc = builder->GetInsertBlock()->getParent();
    auto* okBB = llvm::BasicBlock::Create(*context, "try.ok", llvmFunc);
    auto* errBB = llvm::BasicBlock::Create(*context, "try.err", llvmFunc);
    auto* mergeBB = llvm::BasicBlock::Create(*context, "try.merge", llvmFunc);

    builder->CreateCondBr(errorFlag, errBB, okBB);

    builder->SetInsertPoint(okBB);
    builder->CreateBr(mergeBB);

    builder->SetInsertPoint(errBB);
    errno_clear_flag();
    auto* fallbackVal = generate_expression(*expr.fallback);
    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(fallbackVal))
    {
        fallbackVal = builder->CreateLoad(alloca->getAllocatedType(), alloca, "try_fb_load");
    }
    builder->CreateBr(mergeBB);

    builder->SetInsertPoint(mergeBB);
    auto* phi = builder->CreatePHI(callResult->getType(), 2, "try_result");
    phi->addIncoming(callResult, okBB);
    phi->addIncoming(fallbackVal, errBB);
    return phi;
}
