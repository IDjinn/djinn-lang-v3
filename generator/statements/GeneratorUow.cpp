//
// Transactional statement lowering (VERIFICATION-SPEC.md §10): the lock
// statement, the rollback statement with its entry snapshots, and the inline
// before_commit/after_commit lifecycle blocks deferred to the member's
// normal exit.
//

#include "../Generator.h"

// `a` / `a.b.c` access chain as a field path; false for anything else.
static bool build_access_path(const Expression& expr, std::vector<std::string>& out)
{
    if (const auto* ident = dynamic_cast<const Identifier*>(&expr))
    {
        out.push_back(ident->identifier.token_name);
        return true;
    }
    if (const auto* field = dynamic_cast<const FieldAccess*>(&expr))
    {
        if (!build_access_path(*field->object, out)) return false;
        out.push_back(field->fieldName.token_name);
        return true;
    }
    return false;
}

llvm::Value* Generator::generate_lvalue_address(const Expression& expr)
{
    if (const auto* ident = dynamic_cast<const Identifier*>(&expr))
    {
        if (auto* alloca = currentScope->lookup_variable(ident->identifier.token_name))
        {
            if (alloca->getAllocatedType()->isStructTy()) return alloca;
            if (alloca->getAllocatedType()->isPointerTy())
                return builder->CreateLoad(alloca->getAllocatedType(), alloca, "ref.obj");
        }
    }
    else if (const auto* field = dynamic_cast<const FieldAccess*>(&expr))
    {
        std::vector<std::string> path;
        if (build_access_path(*field, path))
        {
            llvm::Type* valueType = nullptr;
            if (llvm::Value* address = resolve_path_address(path, &valueType, field->fieldName.location))
                return address;
        }
    }
    GENERATOR_ERROR(DiagnosticCode::TYPE_MISMATCH,
                    "argumento para parâmetro por referência de membro de uow deve ser "
                        "uma variável mutável ou uma cadeia de campos",
                    expr.location);
}

llvm::Value* Generator::resolve_object_address(const Expression& expr)
{
    if (const auto* ident = dynamic_cast<const Identifier*>(&expr))
    {
        if (llvm::AllocaInst* alloca = currentScope->lookup_variable(ident->identifier.token_name))
        {
            if (alloca->getAllocatedType()->isStructTy()) return alloca;
            if (alloca->getAllocatedType()->isPointerTy())
            {
                return builder->CreateLoad(alloca->getAllocatedType(), alloca, "lock.obj");
            }
            return alloca;
        }
    }
    return generate_expression(expr);
}

llvm::Value* Generator::resolve_path_address(const std::vector<std::string>& path, llvm::Type** outType,
                                             const SourceLocation& loc)
{
    if (path.empty()) return nullptr;
    if (outType) *outType = nullptr;

    llvm::AllocaInst* root = currentScope->lookup_variable(path.front());
    if (!root) return nullptr;

    std::string structName = currentScope->lookup_variable_struct_type(path.front());
    llvm::Value* addr = root;
    if (!structName.empty() && root->getAllocatedType()->isPointerTy())
    {
        addr = builder->CreateLoad(root->getAllocatedType(), root, "rb.root");
    }

    for (size_t i = 1; i < path.size(); i++)
    {
        if (structName.empty())
        {
            GENERATOR_ERROR(DiagnosticCode::UNEXPECTED_TOKEN,
                            "rollback path '" + path.front() + ".*' traverses a non-struct value",
                            loc);
        }
        const auto* fieldIndices = currentScope->get_field_indices(structName);
        auto* structType = currentScope->get_llvm_struct(structName);
        if (!fieldIndices || !structType)
        {
            GENERATOR_ERROR(DiagnosticCode::UNEXPECTED_TOKEN,
                            "rollback path segment '" + path[i] + "': struct '" + structName +
                                "' has no field layout",
                            loc);
        }
        const auto it = fieldIndices->find(path[i]);
        if (it == fieldIndices->end())
        {
            GENERATOR_ERROR(DiagnosticCode::UNEXPECTED_TOKEN,
                            "rollback path: struct '" + structName + "' has no field '" + path[i] + "'",
                            loc);
        }
        addr = builder->CreateStructGEP(structType, addr, it->second, path[i] + "_ptr");
        llvm::Type* fieldType = structType->getElementType(it->second);
        if (outType) *outType = fieldType;

        if (i + 1 < path.size())
        {
            // Walk into a nested struct field: resolve its name from the layout.
            if (auto* def = currentScope->lookup_struct(structName))
            {
                structName = def->fields[it->second].second.structName;
            }
            else
            {
                structName.clear();
            }
        }
    }

    if (outType && *outType)
    {
        return addr;
    }

    // A bare variable path: the value type is the alloca's allocated type.
    if (auto* alloca = llvm::dyn_cast_or_null<llvm::AllocaInst>(addr))
    {
        *outType = alloca->getAllocatedType();
    }
    return addr;
}

void Generator::collect_rollback_paths(const Block& block, std::vector<std::vector<std::string>>& out) const
{
    for (const auto& stmt : block.statements)
    {
        if (const auto* rollback = dynamic_cast<const RollbackStatement*>(stmt.get()))
        {
            for (const auto& path : rollback->paths)
            {
                if (std::ranges::find(out, path) == out.end()) out.push_back(path);
            }
        }
        else if (const auto* nested = dynamic_cast<const Block*>(stmt.get()))
        {
            collect_rollback_paths(*nested, out);
        }
        else if (const auto* lock = dynamic_cast<const LockStatement*>(stmt.get()))
        {
            if (lock->body) collect_rollback_paths(*lock->body, out);
        }
        else if (const auto* phase = dynamic_cast<const UowPhaseBlockStatement*>(stmt.get()))
        {
            if (phase->body) collect_rollback_paths(*phase->body, out);
        }
        else if (const auto* tryCatch = dynamic_cast<const TryCatchStatement*>(stmt.get()))
        {
            if (tryCatch->tryBlock) collect_rollback_paths(*tryCatch->tryBlock, out);
            for (const auto& clause : tryCatch->catches)
            {
                if (clause.body) collect_rollback_paths(*clause.body, out);
            }
            if (tryCatch->finallyBlock) collect_rollback_paths(*tryCatch->finallyBlock, out);
        }
        else if (const auto* ifStmt = dynamic_cast<const IfStatement*>(stmt.get()))
        {
            if (ifStmt->thenBranch) collect_rollback_paths(*ifStmt->thenBranch, out);
            if (ifStmt->elseBranch) collect_rollback_paths(*ifStmt->elseBranch, out);
        }
    }
}

void Generator::setup_rollback_snapshots(const std::vector<const CatchClause*>& arms,
                                         const std::vector<std::string>& paramNames,
                                         const std::vector<Type>& paramTypes,
                                         const std::string& selfStructName)
{
    rollbackSnapshots_.clear();
    deferredBeforeCommitBlocks_.clear();
    deferredAfterCommitBlocks_.clear();
    if (arms.empty()) return;

    bool sawBare = false;
    std::vector<std::vector<std::string>> listed;
    for (const auto* arm : arms)
    {
        if (!arm->body) continue;
        for (const auto& stmt : arm->body->statements)
        {
            if (dynamic_cast<const RollbackStatement*>(stmt.get()))
            {
                if (const auto* rollback = dynamic_cast<const RollbackStatement*>(stmt.get());
                    rollback && rollback->paths.empty())
                    sawBare = true;
            }
        }
        collect_rollback_paths(*arm->body, listed);
    }
    if (!sawBare && listed.empty()) return;

    if (sawBare)
    {
        // Whole-object snapshots: every struct parameter plus self. The entry
        // copy is what a bare `rollback;` restores — the member runs or it
        // does not run, regardless of which callees mutated what. By-reference
        // parameters (uow members) restore through the pointer, so the
        // caller's object is what gets rolled back.
        for (size_t i = 0; i < paramNames.size() && i < paramTypes.size(); i++)
        {
            const bool byValue = paramTypes[i].kind == TypeKind::STRUCT;
            const bool byReference = paramTypes[i].kind == TypeKind::POINTER && paramTypes[i].elementType &&
                                     paramTypes[i].elementType->kind == TypeKind::STRUCT;
            if (!byValue && !byReference) continue;
            auto* alloca = currentScope->lookup_variable(paramNames[i]);
            if (!alloca) continue;

            if (byValue)
            {
                if (!alloca->getAllocatedType()->isStructTy()) continue;
                auto* slot = builder->CreateAlloca(alloca->getAllocatedType(), nullptr, "rb.slot");
                builder->CreateStore(
                    builder->CreateLoad(alloca->getAllocatedType(), alloca, "rb.entry"), slot);
                rollbackSnapshots_.push_back(
                    {{paramNames[i]}, alloca, alloca->getAllocatedType(), slot});
            }
            else
            {
                const std::string structName = currentScope->lookup_variable_struct_type(paramNames[i]);
                auto* structType = structName.empty() ? nullptr : currentScope->get_llvm_struct(structName);
                if (!structType) continue;
                auto* address = builder->CreateLoad(alloca->getAllocatedType(), alloca, "rb.obj");
                auto* slot = builder->CreateAlloca(structType, nullptr, "rb.slot");
                builder->CreateStore(builder->CreateLoad(structType, address, "rb.entry"), slot);
                rollbackSnapshots_.push_back({{paramNames[i]}, address, structType, slot});
            }
        }
        if (!selfStructName.empty())
        {
            if (auto* thisAlloca = currentScope->lookup_variable("this"))
            {
                if (auto* structType = currentScope->get_llvm_struct(selfStructName))
                {
                    auto* address = builder->CreateLoad(thisAlloca->getAllocatedType(), thisAlloca,
                                                        "rb.self");
                    auto* slot = builder->CreateAlloca(structType, nullptr, "rb.slot");
                    builder->CreateStore(builder->CreateLoad(structType, address, "rb.entry"), slot);
                    rollbackSnapshots_.push_back({{"this"}, address, structType, slot});
                }
            }
        }
    }

    for (const auto& path : listed)
    {
        if (path.size() == 1)
        {
            // A bare-variable path snapshots the whole object; skip when the
            // bare form already captured it.
            bool captured = false;
            for (const auto& snapshot : rollbackSnapshots_)
                if (snapshot.path == path) captured = true;
            if (captured) continue;
        }
        llvm::Type* valueType = nullptr;
        llvm::Value* address = resolve_path_address(path, &valueType);
        if (!address || !valueType) continue;
        auto* slot = builder->CreateAlloca(valueType, nullptr, "rb.slot");
        builder->CreateStore(builder->CreateLoad(valueType, address, "rb.entry"), slot);
        rollbackSnapshots_.push_back({path, address, valueType, slot});
    }
}

void Generator::generate_rollback_statement(const RollbackStatement& stmt)
{
    if (rollbackSnapshots_.empty()) return; // nothing declared — the verifier governs necessity

    std::vector<const RollbackSnapshot*> selected;
    if (stmt.paths.empty())
    {
        for (const auto& snapshot : rollbackSnapshots_) selected.push_back(&snapshot);
    }
    else
    {
        for (const auto& path : stmt.paths)
        {
            for (const auto& snapshot : rollbackSnapshots_)
            {
                if (snapshot.path == path)
                {
                    selected.push_back(&snapshot);
                    break;
                }
            }
        }
    }

    for (size_t i = selected.size(); i-- > 0;)
    {
        const auto* snapshot = selected[i];
        builder->CreateStore(builder->CreateLoad(snapshot->valueType, snapshot->slot, "rb.saved"),
                             snapshot->address);
    }
}

void Generator::generate_lock_statement(const LockStatement& stmt)
{
    std::vector<llvm::Value*> objects;
    for (const auto& operand : stmt.operands)
    {
        objects.push_back(resolve_object_address(*operand));
    }

    auto* lockFn = module->getFunction("__djinn_object_lock");
    if (!lockFn)
    {
        auto* ty = llvm::FunctionType::get(builder->getVoidTy(), {builder->getPtrTy()}, false);
        lockFn = llvm::Function::Create(ty, llvm::Function::ExternalLinkage,
                                        "__djinn_object_lock", *module);
        lockFn->setCallingConv(llvm::CallingConv::C);
    }
    auto* unlockFn = module->getFunction("__djinn_object_unlock");
    if (!unlockFn)
    {
        auto* ty = llvm::FunctionType::get(builder->getVoidTy(), {builder->getPtrTy()}, false);
        unlockFn = llvm::Function::Create(ty, llvm::Function::ExternalLinkage,
                                          "__djinn_object_unlock", *module);
        unlockFn->setCallingConv(llvm::CallingConv::C);
    }

    for (auto* obj : objects)
    {
        emit_call_or_invoke(lockFn, {obj}, false);
    }

    // Unwind safety: the release runs in a cleanup pad so an exception thrown
    // inside the critical section still unlocks (reverse order).
    const bool unwindLanding = nativeExceptions && currentFunctionThrows;
    NativeLanding landing;
    if (unwindLanding) landing = push_native_landing(true);

    push_scope();
    generate_block(*stmt.body);
    pop_scope();

    auto* func = builder->GetInsertBlock()->getParent();
    auto* contBB = llvm::BasicBlock::Create(*context, "lock.cont", func);

    if (!builder->GetInsertBlock()->getTerminator())
    {
        for (size_t i = objects.size(); i-- > 0;)
        {
            emit_call_or_invoke(unlockFn, {objects[i]}, false);
        }
        builder->CreateBr(contBB);
    }

    if (unwindLanding)
    {
        ehLandingStack_.pop_back();
        finalize_native_landing(landing, nullptr, [this, &objects, unlockFn]
        {
            for (size_t i = objects.size(); i-- > 0;)
            {
                builder->CreateCall(unlockFn, {objects[i]});
            }
        });
    }

    builder->SetInsertPoint(contBB);
}

void Generator::generate_member_body(const Block& body)
{
    bool committed = false;
    for (const auto& stmt : body.statements)
    {
        if (!stmt) continue;
        if (dynamic_cast<const CommitStatement*>(stmt.get()))
        {
            committed = true;
            continue;
        }
        if (committed)
            deferredAfterCommitBlocks_.push_back(stmt.get());
        else
            generate_statement(*stmt);
    }
}

void Generator::emit_deferred_uow_blocks()
{
    if (deferredBeforeCommitBlocks_.empty() && deferredAfterCommitBlocks_.empty()) return;

    auto before = std::move(deferredBeforeCommitBlocks_);
    auto after = std::move(deferredAfterCommitBlocks_);
    deferredBeforeCommitBlocks_.clear();
    deferredAfterCommitBlocks_.clear();

    for (const auto* stmt : before)
    {
        push_scope();
        generate_statement(*stmt);
        pop_scope();
    }
    for (const auto* stmt : after)
    {
        push_scope();
        generate_statement(*stmt);
        pop_scope();
    }
}
