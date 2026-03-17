//
// Created by Luke on 06/12/2025.
//

#include "Generator.h"
#include "visitors/GeneratorStatementVisitor.h"

void Generator::generate_statement(const Statement& stmt)
{
    djinn::GeneratorStatementVisitor visitor(*this);
    stmt.accept(visitor);
}

void Generator::generate_return_statement(const ReturnStatement& stmt)
{
    // In async functions: store return value in promise and branch to final suspend
    if (inAsyncFunction)
    {
        if (stmt.value && asyncPromisePtr && asyncReturnType && !asyncReturnType->isVoidTy())
        {
            llvm::Value* val = nullptr;

            // Handle brace initializers for struct returns (e.g. return { .fd = x, .connected = true })
            if (auto* braceInit = dynamic_cast<const BraceInitializer*>(stmt.value.get()))
            {
                if (auto* structType = llvm::dyn_cast<llvm::StructType>(asyncReturnType))
                {
                    const auto structName = structType->getName().str();
                    val = generate_brace_init_for_struct(*braceInit, structType, structName);
                }
            }

            if (!val)
            {
                val = generate_expression(*stmt.value);
                val = cast_value(val, asyncReturnType);
            }

            // Load from alloca if needed
            if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(val))
            {
                if (alloca->getAllocatedType() == asyncReturnType)
                {
                    val = builder->CreateLoad(asyncReturnType, alloca, "ret_load");
                }
            }

            builder->CreateStore(val, asyncPromisePtr);
        }
        emit_all_scope_cleanup();
        builder->CreateBr(asyncFinalSuspendBB);
        return;
    }

    if (stmt.value)
    {
        if (auto* braceInit = dynamic_cast<const BraceInitializer*>(stmt.value.get()))
        {
            const auto returnType = currentFunction->getReturnType();
            if (auto* structType = llvm::dyn_cast<llvm::StructType>(returnType))
            {
                const auto structName = structType->getName().str();
                if (const auto structVal = generate_brace_init_for_struct(*braceInit, structType, structName))
                {
                    emit_all_scope_cleanup();
                    builder->CreateRet(structVal);
                    return;
                }
            }
        }
        // Determine signedness from source expression for correct cast instruction
        bool sourceSigned = true; // default to signed
        if (const auto* ident = dynamic_cast<const Identifier*>(stmt.value.get()))
        {
            if (auto sign = currentScope->lookup_variable_signed(ident->name()))
            {
                sourceSigned = *sign;
            }
        }

        auto val = generate_expression(*stmt.value);
        llvm::Type* returnType = currentFunction->getReturnType();
        // Load struct from alloca for return (e.g., string literal returning str)
        if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(val))
        {
            if (alloca->getAllocatedType() == returnType)
            {
                val = builder->CreateLoad(returnType, alloca, "ret_load");
            }
        }
        val = cast_value(val, returnType, sourceSigned);
        emit_all_scope_cleanup();
        builder->CreateRet(val);
    }
    else
    {
        emit_all_scope_cleanup();
        builder->CreateRetVoid();
    }
}

void Generator::generate_yield_statement()
{
    if (!inAsyncFunction)
    {
        throw CompileError(DiagnosticCode::UNEXPECTED_TOKEN, "yield só é permitido em funções async");
    }

    // Intermediate suspend: i1 false (not final)
    auto* coroSuspendFn = llvm::Intrinsic::getOrInsertDeclaration(
        module.get(), llvm::Intrinsic::coro_suspend);
    llvm::Value* suspResult = builder->CreateCall(coroSuspendFn, {
                                                      llvm::ConstantTokenNone::get(*context),
                                                      builder->getFalse() // false = intermediate (not final)
                                                  }, "coro.yield");

    // Switch: 0 = resumed, 1 = destroyed, default = suspend (return to caller)
    auto* resumeBB = llvm::BasicBlock::Create(*context, "yield.resume", currentFunction);
    auto* switchInst = builder->CreateSwitch(suspResult, asyncSuspendBB, 2);
    switchInst->addCase(builder->getInt8(0), resumeBB);
    switchInst->addCase(builder->getInt8(1), asyncCleanupBB);

    // Continue execution after yield
    builder->SetInsertPoint(resumeBB);
}

void Generator::generate_spawn_statement(const SpawnStatement& stmt)
{
    // Generate the async function call (returns coroutine handle)
    llvm::Value* handle = generate_expression(*stmt.expression);

    // Call __djinn_spawn(handle) to submit to event loop
    auto* spawnFn = module->getFunction("__djinn_spawn");
    if (!spawnFn)
    {
        auto* ptrTy = llvm::PointerType::getUnqual(*context);
        auto* ft = llvm::FunctionType::get(builder->getVoidTy(), {ptrTy}, false);
        spawnFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                         "__djinn_spawn", *module);
    }
    builder->CreateCall(spawnFn, {handle});
}

void Generator::generate_break_statement()
{
    if (!breakTargets.empty())
    {
        emit_scope_cleanup();
        builder->CreateBr(breakTargets.back());
    }
}

void Generator::generate_continue_statement()
{
    if (!continueTargets.empty())
    {
        emit_scope_cleanup();
        builder->CreateBr(continueTargets.back());
    }
}

void Generator::generate_block(const Block& block)
{
    push_scope();
    for (const auto& stmt : block.statements)
    {
        generate_statement(*stmt);
        if (builder->GetInsertBlock()->getTerminator())
        {
            break;
        }
    }
    emit_scope_cleanup();
    pop_scope();
}

void Generator::generate_if_statement(const IfStatement& stmt)
{
    llvm::Value* condValue = generate_expression(*stmt.condition);

    if (!condValue->getType()->isIntegerTy(1))
    {
        condValue = builder->CreateICmpNE(
            condValue,
            llvm::ConstantInt::get(condValue->getType(), 0),
            "ifcond"
        );
    }

    llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*context, "then", currentFunction);
    llvm::BasicBlock* elseBB = stmt.elseBranch
                                   ? llvm::BasicBlock::Create(*context, "else", currentFunction)
                                   : nullptr;
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "ifcont", currentFunction);

    if (elseBB)
    {
        builder->CreateCondBr(condValue, thenBB, elseBB);
    }
    else
    {
        builder->CreateCondBr(condValue, thenBB, mergeBB);
    }

    builder->SetInsertPoint(thenBB);
    if (stmt.thenBranch)
    {
        generate_block(*stmt.thenBranch);
    }
    if (!builder->GetInsertBlock()->getTerminator())
    {
        builder->CreateBr(mergeBB);
    }

    if (elseBB)
    {
        builder->SetInsertPoint(elseBB);
        if (stmt.elseBranch)
        {
            generate_block(*stmt.elseBranch);
        }
        if (!builder->GetInsertBlock()->getTerminator())
        {
            builder->CreateBr(mergeBB);
        }
    }

    builder->SetInsertPoint(mergeBB);
}

void Generator::generate_for_statement(const ForStatement& stmt)
{
    push_scope();

    if (stmt.initializer)
    {
        generate_expression(*stmt.initializer);
    }

    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "for.cond", currentFunction);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(*context, "for.body", currentFunction);
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(*context, "for.inc", currentFunction);
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(*context, "for.end", currentFunction);

    breakTargets.push_back(endBB);
    continueTargets.push_back(incBB);

    builder->CreateBr(condBB);

    builder->SetInsertPoint(condBB);
    if (stmt.condition)
    {
        llvm::Value* condValue = generate_expression(*stmt.condition);
        if (!condValue->getType()->isIntegerTy(1))
        {
            condValue = builder->CreateICmpNE(
                condValue,
                llvm::ConstantInt::get(condValue->getType(), 0),
                "forcond"
            );
        }
        builder->CreateCondBr(condValue, bodyBB, endBB);
    }
    else
    {
        builder->CreateBr(bodyBB);
    }

    builder->SetInsertPoint(bodyBB);
    if (stmt.body)
    {
        generate_block(*stmt.body);
    }
    if (!builder->GetInsertBlock()->getTerminator())
    {
        builder->CreateBr(incBB);
    }

    builder->SetInsertPoint(incBB);
    if (stmt.postfix)
    {
        generate_expression(*stmt.postfix);
    }
    builder->CreateBr(condBB);

    builder->SetInsertPoint(endBB);

    breakTargets.pop_back();
    continueTargets.pop_back();

    pop_scope();
}

void Generator::generate_while_statement(const WhileStatement& stmt)
{
    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "while.cond", currentFunction);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(*context, "while.body", currentFunction);
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(*context, "while.end", currentFunction);

    breakTargets.push_back(endBB);
    continueTargets.push_back(condBB);

    builder->CreateBr(condBB);
    builder->SetInsertPoint(condBB);
    llvm::Value* condValue = generate_expression(*stmt.condition);
    if (!condValue->getType()->isIntegerTy(1))
    {
        condValue = builder->CreateICmpNE(
            condValue,
            llvm::ConstantInt::get(condValue->getType(), 0),
            "whilecond"
        );
    }
    builder->CreateCondBr(condValue, bodyBB, endBB);
    builder->SetInsertPoint(bodyBB);

    if (stmt.body)
    {
        generate_block(*stmt.body);
    }
    if (!builder->GetInsertBlock()->getTerminator())
    {
        builder->CreateBr(condBB);
    }

    builder->SetInsertPoint(endBB);

    breakTargets.pop_back();
    continueTargets.pop_back();
}

void Generator::generate_do_while_statement(const DoWhileStatement& stmt)
{
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(*context, "dowhile.body", currentFunction);
    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "dowhile.cond", currentFunction);
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(*context, "dowhile.end", currentFunction);

    breakTargets.push_back(endBB);
    continueTargets.push_back(condBB);

    builder->CreateBr(bodyBB);
    builder->SetInsertPoint(bodyBB);
    if (stmt.body)
    {
        generate_block(*stmt.body);
    }
    if (!builder->GetInsertBlock()->getTerminator())
    {
        builder->CreateBr(condBB);
    }

    builder->SetInsertPoint(condBB);
    llvm::Value* condValue = generate_expression(*stmt.condition);
    if (!condValue->getType()->isIntegerTy(1))
    {
        condValue = builder->CreateICmpNE(
            condValue,
            llvm::ConstantInt::get(condValue->getType(), 0),
            "dowhilecond"
        );
    }
    builder->CreateCondBr(condValue, bodyBB, endBB);
    builder->SetInsertPoint(endBB);

    breakTargets.pop_back();
    continueTargets.pop_back();
}

void Generator::generate_switch_statement(const SwitchStatement& stmt)
{
    llvm::Value* switchValue = generate_expression(*stmt.value);

    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(*context, "switch.end", currentFunction);
    llvm::BasicBlock* defaultBB = endBB;
    breakTargets.push_back(endBB);

    size_t numCases = 0;
    for (const auto& caseStmt : stmt.cases)
    {
        if (caseStmt->expression)
        {
            numCases++;
        }
        else
        {
            defaultBB = llvm::BasicBlock::Create(*context, "switch.default", currentFunction);
        }
    }

    llvm::SwitchInst* switchInst = builder->CreateSwitch(switchValue, defaultBB, numCases);

    for (const auto& caseStmt : stmt.cases)
    {
        llvm::BasicBlock* caseBB;

        if (caseStmt->expression)
        {
            caseBB = llvm::BasicBlock::Create(*context, "switch.case", currentFunction);
            llvm::Value* caseValue = generate_expression(*caseStmt->expression);

            // Cast case value to match switch value type
            if (caseValue->getType() != switchValue->getType())
            {
                if (const auto* caseConst = llvm::dyn_cast<llvm::ConstantInt>(caseValue))
                {
                    caseValue = llvm::ConstantInt::get(switchValue->getType(),
                                                       caseConst->getSExtValue());
                }
            }

            if (auto* caseConst = llvm::dyn_cast<llvm::ConstantInt>(caseValue))
            {
                switchInst->addCase(caseConst, caseBB);
            }
        }
        else
        {
            caseBB = defaultBB;
        }

        builder->SetInsertPoint(caseBB);
        if (caseStmt->body)
        {
            generate_block(*caseStmt->body);
        }
        if (!builder->GetInsertBlock()->getTerminator())
        {
            builder->CreateBr(endBB);
        }
    }

    builder->SetInsertPoint(endBB);
    breakTargets.pop_back();
}

llvm::Value* Generator::generate_brace_init_for_struct(const BraceInitializer& braceInit, llvm::StructType* structType,
                                                       const std::string& structName)
{
    auto* alloca = builder->CreateAlloca(structType, nullptr, "struct_init");
    builder->CreateStore(llvm::Constant::getNullValue(structType), alloca);

    const auto* fieldIndices = currentScope->get_field_indices(structName);
    if (!fieldIndices)
    {
        throw CompileError(DiagnosticCode::UNDEFINED_STRUCT, "struct não encontrada: " + structName);
    }

    for (size_t i = 0; i < braceInit.elements.size(); ++i)
    {
        const auto& elem = braceInit.elements[i];
        llvm::Value* val = generate_expression(*elem.value);
        if (!val) return nullptr;

        unsigned fieldIdx;
        if (elem.isDesignated())
        {
            auto it = fieldIndices->find(elem.fieldName.token_name);
            if (it == fieldIndices->end())
            {
                throw CompileError(DiagnosticCode::UNDEFINED_FIELD,
                                   "campo não encontrado: " + elem.fieldName.token_name);
            }
            fieldIdx = it->second;
        }
        else
        {
            fieldIdx = static_cast<unsigned>(i);
        }

        llvm::Type* fieldType = structType->getElementType(fieldIdx);
        val = cast_value(val, fieldType);

        auto* fieldPtr = builder->CreateStructGEP(structType, alloca, fieldIdx);
        builder->CreateStore(val, fieldPtr);
    }

    return builder->CreateLoad(structType, alloca, "struct_val");
}

llvm::Function* Generator::find_destroy_method(llvm::AllocaInst* alloca)
{
    if (!alloca) return nullptr;

    llvm::Type* allocatedType = alloca->getAllocatedType();

    // Only struct types can have destroy methods
    auto* structType = llvm::dyn_cast<llvm::StructType>(allocatedType);
    if (!structType || !structType->hasName()) return nullptr;

    std::string structName = structType->getName().str();

    // Look up destroy in the struct's method functions
    if (const StructDef* def = currentScope->lookup_struct(structName))
    {
        if (auto it = def->methodFunctions.find("destroy"); it != def->methodFunctions.end())
        {
            return it->second;
        }
    }

    // Also try the mangled name directly
    std::string destroyName = structName + "__destroy";
    if (auto it = functions.find(destroyName); it != functions.end())
    {
        return it->second;
    }

    return nullptr;
}

void Generator::emit_scope_cleanup()
{
    if (builder->GetInsertBlock()->getTerminator()) return;

    // Iterate in reverse order (LIFO) — last declared, first destroyed
    for (auto it = currentScope->cleanupList.rbegin(); it != currentScope->cleanupList.rend(); ++it)
    {
        // Never destroy 'this'
        if (it->varName == "this") continue;

        llvm::Function* destroyFunc = find_destroy_method(it->alloca);
        if (!destroyFunc) continue;

        builder->CreateCall(destroyFunc, {it->alloca});
    }
}

void Generator::emit_all_scope_cleanup()
{
    if (builder->GetInsertBlock()->getTerminator()) return;

    // Walk from current scope up to the top, emitting cleanup for each
    for (auto scope = currentScope; scope != nullptr; scope = scope->parent)
    {
        for (auto it = scope->cleanupList.rbegin(); it != scope->cleanupList.rend(); ++it)
        {
            if (it->varName == "this") continue;

            llvm::Function* destroyFunc = find_destroy_method(it->alloca);
            if (!destroyFunc) continue;

            builder->CreateCall(destroyFunc, {it->alloca});
        }
    }
}