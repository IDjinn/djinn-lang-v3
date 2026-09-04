//
// Created by Luke on 06/12/2025.
//

#include "Generator.h"
#include "ErrorTagMatching.h"
#include "visitors/GeneratorExpressionVisitor.h"

llvm::Value* Generator::generate_expression(const Expression& expr)
{
    djinn::GeneratorExpressionVisitor visitor(*this);
    expr.accept(visitor);
    return visitor.result();
}

// ============================================================================
// Switch Expression Generation
// ============================================================================
//
// For enums: switch opt { Value val -> val, Empty -> -1 }
//
// 1. Extract the tag from the enum
// 2. Switch on the tag
// 3. For each arm, create a block that:
//    - Extracts payload if there's a binding
//    - Evaluates the result expression
//    - Branches to merge
// 4. Merge with phi node to collect results
//
// For throwing operands: switch division(1, 0) { Result v -> v, Error e -> -1 }
//
// 1. Evaluate the operand with propagation suppressed
// 2. Branch on the error flag
// 3. Success path: first Result/_ arm, binding the call result (enum operands
//    keep their variant dispatch, with _ as the catch-all variant)
// 4. Error path: arms matched in source order by error tag (specific types
//    match derived errors too; Error matches anything), binding the thrown
//    error value; an unmatched error propagates or aborts uncaught
// ============================================================================
llvm::Value* Generator::generate_switch_expression(const SwitchExpression& expr)
{
    ensure_error_globals_declared();

    // The operand may be a throwing call whose outcome takes part in the match:
    // clear the flag and suppress auto-propagation while it evaluates. Native
    // mode adds a landing so the operand's throwing calls unwind back to a
    // dedicated check block with the error state set by the shim.
    errno_clear_flag();

    NativeLanding ehLanding;
    const bool nativeLanding = nativeExceptions;
    if (nativeLanding)
    {
        ehLanding = push_native_landing(false);
    }

    const bool prevInsideTry = insideTryOperand_;
    insideTryOperand_ = true;
    llvm::Value* matchValue = generate_expression(*expr.value);
    insideTryOperand_ = prevInsideTry;

    if (!matchValue)
    {
        GENERATOR_ERROR(DiagnosticCode::UNEXPECTED_TOKEN, "Failed to generate switch expression value",
                        expr.value->location);
    }

    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(matchValue))
    {
        matchValue = builder->CreateLoad(alloca->getAllocatedType(), alloca, "switch_load");
    }

    llvm::Value* errorFlag = nullptr;
    if (nativeLanding)
    {
        auto* checkBB = llvm::BasicBlock::Create(*context, "switch.check", builder->GetInsertBlock()->getParent());
        builder->CreateBr(checkBB);
        builder->SetInsertPoint(checkBB);
        errorFlag = errno_load_flag("switch_err_flag");
        finalize_native_landing(ehLanding, checkBB);
        ehLandingStack_.pop_back();
    }
    else
    {
        errorFlag = errno_load_flag("switch_err_flag");
    }

    const EnumDef* enumDef = nullptr;
    if (matchValue->getType()->isStructTy())
    {
        enumDef = currentScope->lookup_enum(matchValue->getType()->getStructName().str());
    }

    struct GeneratedArm
    {
        llvm::BasicBlock* block;
        llvm::Value* value;
        const SwitchArm* arm;
    };
    std::vector<GeneratedArm> armResults;

    // Block arms run their statements and end in 'throw' (diverging, no value)
    // or 'yield expr' (arm value, execution continues at the merge);
    // they contribute no incoming edge to the merge phi when diverging
    auto generate_arm_body = [this](const SwitchArm& arm) -> llvm::Value*
    {
        if (arm.block)
        {
            auto* yieldBB = llvm::BasicBlock::Create(*context, "switch.arm.yield",
                                                     builder->GetInsertBlock()->getParent());
            const auto prevYieldBlock = armYieldBlock_;
            const auto prevYieldSlot = armYieldSlot_;
            const auto prevDidYield = armDidYield_;
            const auto prevInArm = inSwitchArmBlock_;
            armYieldBlock_ = yieldBB;
            armYieldSlot_ = nullptr;
            armDidYield_ = false;
            inSwitchArmBlock_ = true;

            generate_block(*arm.block);

            auto* yieldSlot = armYieldSlot_;
            const bool didYield = armDidYield_;
            armYieldBlock_ = prevYieldBlock;
            armYieldSlot_ = prevYieldSlot;
            armDidYield_ = prevDidYield;
            inSwitchArmBlock_ = prevInArm;

            // Fall-through is only valid where no value is needed: the final
            // block must be terminated, or already dead (e.g. both paths of
            // an if/else threw) with the arm value delivered by a yield
            auto* endBlock = builder->GetInsertBlock();
            if (!endBlock->getTerminator() && !endBlock->hasNPredecessors(0))
            {
                GENERATOR_ERROR(DiagnosticCode::UNEXPECTED_TOKEN,
                                "Switch arm block must end with 'throw' or 'yield'",
                                arm.variantName.location);
            }

            if (didYield)
            {
                builder->SetInsertPoint(yieldBB);
                return builder->CreateLoad(yieldSlot->getAllocatedType(), yieldSlot, "arm_yield_val");
            }

            yieldBB->eraseFromParent();
            return nullptr;
        }
        return generate_expression(*arm.result);
    };

    const SwitchArm* okArm = nullptr;       // first Result/_ arm (non-enum success)
    const SwitchArm* wildcardArm = nullptr; // enum catch-all arm
    std::vector<const SwitchArm*> errorArms;

    for (const auto& arm : expr.arms)
    {
        const auto& name = arm.variantName.token_name;

        if (enumDef)
        {
            if (name == "_")
            {
                if (arm.binding)
                {
                    GENERATOR_ERROR(DiagnosticCode::UNEXPECTED_TOKEN, "Wildcard arm cannot bind a value",
                                    arm.variantName.location);
                }
                wildcardArm = &arm;
            }
            else if (enumDef->getVariant(name))
            {
                continue;
            }
            else if (name == "Error" || resolve_error_struct(name))
            {
                errorArms.push_back(&arm);
            }
            else
            {
                GENERATOR_ERROR(DiagnosticCode::UNEXPECTED_TOKEN, "Unknown variant: " + name,
                                arm.variantName.location);
            }
        }
        else
        {
            if (name == "Result" || name == "_")
            {
                if (!okArm) okArm = &arm;
            }
            else if (name == "Error" || resolve_error_struct(name))
            {
                errorArms.push_back(&arm);
            }
            else
            {
                GENERATOR_ERROR(DiagnosticCode::UNEXPECTED_TOKEN, "Unknown error type in switch arm: " + name,
                                arm.variantName.location);
            }
        }
    }

    auto* func = builder->GetInsertBlock()->getParent();
    auto* mergeBlock = llvm::BasicBlock::Create(*context, "switch_merge", func);
    auto* okBB = llvm::BasicBlock::Create(*context, "switch.ok", func);
    auto* errBB = llvm::BasicBlock::Create(*context, "switch.err", func);

    builder->CreateCondBr(errorFlag, errBB, okBB);

    // ---- Success path ----
    builder->SetInsertPoint(okBB);

    if (enumDef)
    {
        llvm::Value* tag = extract_enum_tag(matchValue, *enumDef);

        auto* defaultBlock = llvm::BasicBlock::Create(*context, "switch_default", func);
        llvm::SwitchInst* switchInst = builder->CreateSwitch(tag, defaultBlock, expr.arms.size());

        for (const auto& arm : expr.arms)
        {
            const auto& name = arm.variantName.token_name;
            if (name == "_" || !enumDef->getVariant(name)) continue;

            const EnumVariantDef* variant = enumDef->getVariant(name);

            auto* armBlock = llvm::BasicBlock::Create(*context, "case_" + name, func);
            switchInst->addCase(llvm::ConstantInt::get(
                static_cast<llvm::IntegerType*>(enumDef->tagType), variant->tag), armBlock);

            builder->SetInsertPoint(armBlock);
            push_scope();

            if (arm.binding)
            {
                size_t variantIdx = 0;
                for (size_t i = 0; i < enumDef->variants.size(); ++i)
                {
                    if (enumDef->variants[i].name.token_name == name)
                    {
                        variantIdx = i;
                        break;
                    }
                }

                llvm::Value* payload = extract_enum_payload(matchValue, *enumDef, variantIdx);
                if (payload)
                {
                    auto* bindingAlloca = builder->CreateAlloca(payload->getType(), nullptr,
                                                                arm.binding->token_name);
                    builder->CreateStore(payload, bindingAlloca);
                    currentScope->define_variable(arm.binding->token_name, bindingAlloca);
                }
            }

            auto* result = generate_arm_body(arm);
            pop_scope();
            if (result)
            {
                armResults.push_back({builder->GetInsertBlock(), result, &arm});
                builder->CreateBr(mergeBlock);
            }
        }

        builder->SetInsertPoint(defaultBlock);
        if (wildcardArm)
        {
            push_scope();
            auto* result = generate_arm_body(*wildcardArm);
            pop_scope();
            if (result)
            {
                armResults.push_back({builder->GetInsertBlock(), result, wildcardArm});
                builder->CreateBr(mergeBlock);
            }
        }
        else
        {
            builder->CreateUnreachable();
        }
    }
    else
    {
        if (!okArm)
        {
            GENERATOR_ERROR(DiagnosticCode::UNEXPECTED_TOKEN,
                            "Switch over a non-enum value requires a 'Result' or '_' arm", expr.location);
        }

        push_scope();
        if (okArm->binding && !matchValue->getType()->isVoidTy())
        {
            auto* bindingAlloca = builder->CreateAlloca(matchValue->getType(), nullptr,
                                                        okArm->binding->token_name);
            builder->CreateStore(matchValue, bindingAlloca);
            currentScope->define_variable(okArm->binding->token_name, bindingAlloca);
        }
        auto* result = generate_arm_body(*okArm);
        pop_scope();
        if (result)
        {
            armResults.push_back({builder->GetInsertBlock(), result, okArm});
            builder->CreateBr(mergeBlock);
        }
    }

    // ---- Error path ----
    builder->SetInsertPoint(errBB);
    auto* thrownTag = errno_load_i32(1, "switch_err_tag");

    bool caughtAll = false;
    for (size_t i = 0; i < errorArms.size() && !caughtAll; ++i)
    {
        const SwitchArm* arm = errorArms[i];
        auto* armBlock = llvm::BasicBlock::Create(*context, "switch.catch." + arm->variantName.token_name, func);
        const auto errSym = resolve_error_struct(arm->variantName.token_name);

        llvm::BasicBlock* continueBlock = nullptr;
        if (errSym)
        {
            continueBlock = llvm::BasicBlock::Create(*context, "switch.catch.next", func);
            llvm::Value* matched = nullptr;
            for (const int32_t tag : djinn::error_arm_matched_tags(*symbols, *errSym))
            {
                auto* cmp = builder->CreateICmpEQ(thrownTag, builder->getInt32(tag), "switch_tag_cmp");
                matched = matched ? builder->CreateOr(matched, cmp) : cmp;
            }
            builder->CreateCondBr(matched, armBlock, continueBlock);
        }
        else
        {
            if (i + 1 < errorArms.size())
            {
                GENERATOR_WARN(DiagnosticCode::SWITCH_ARM_UNREACHABLE,
                               "switch arms after 'Error' can never match", arm->variantName.location);
            }
            builder->CreateBr(armBlock);
            caughtAll = true;
        }

        builder->SetInsertPoint(armBlock);
        errno_clear_flag();

        push_scope();
        if (arm->binding)
        {
            auto* errType = djinn_error_value_type(*context, *builder);
            auto* errAlloca = builder->CreateAlloca(errType, nullptr, arm->binding->token_name);
            auto* nameVal = errno_load_ptr(3, "switch_err_type");
            auto* msgVal = errno_load_ptr(2, "switch_err_msg");
            builder->CreateStore(thrownTag, builder->CreateStructGEP(errType, errAlloca, 0, "bind_tag_ptr"));
            builder->CreateStore(msgVal, builder->CreateStructGEP(errType, errAlloca, 1, "bind_msg_ptr"));
            builder->CreateStore(nameVal, builder->CreateStructGEP(errType, errAlloca, 2, "bind_type_ptr"));
            currentScope->define_variable(arm->binding->token_name, errAlloca);
        }
        auto* result = generate_arm_body(*arm);
        pop_scope();
        if (result)
        {
            armResults.push_back({builder->GetInsertBlock(), result, arm});
            builder->CreateBr(mergeBlock);
        }

        if (continueBlock)
        {
            builder->SetInsertPoint(continueBlock);
        }
    }

    // No error arm matched: propagate in throwing functions, abort uncaught otherwise
    if (!caughtAll)
    {
        if (currentFunctionThrows)
        {
            store_error_origin(expr.location);
            emit_error_return_path();
        }
        else
        {
            emit_uncaught_error_trap();
        }
    }

    // ---- Merge ----
    builder->SetInsertPoint(mergeBlock);

    if (armResults.empty())
    {
        if (expr.arms.empty())
        {
            GENERATOR_ERROR(DiagnosticCode::UNEXPECTED_TOKEN, "Switch expression must have at least one arm",
                            expr.location);
        }
        // Every arm diverged (throw): the merge point is unreachable
        builder->CreateUnreachable();
        return nullptr;
    }

    llvm::Type* resultType = armResults[0].value->getType();
    for (const auto& generated : armResults)
    {
        if (generated.value->getType() != resultType)
        {
            GENERATOR_ERROR(DiagnosticCode::TYPE_MISMATCH,
                            "Switch arms must yield the same type", generated.arm->result->location);
        }
    }

    llvm::PHINode* phi = builder->CreatePHI(resultType, armResults.size(), "switch_result");
    for (const auto& generated : armResults)
    {
        phi->addIncoming(generated.value, generated.block);
    }

    return phi;
}
