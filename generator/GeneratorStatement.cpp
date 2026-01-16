//
// Created by Luke on 06/12/2025.
//

#include "Generator.h"

void Generator::generate_statement(const Statement &stmt) {
    if (auto *retStmt = dynamic_cast<const ReturnStatement *>(&stmt)) {
        if (retStmt->value) {
            if (auto *braceInit = dynamic_cast<const BraceInitializer *>(retStmt->value.get())) {
                const auto returnType = currentFunction->getReturnType();
                if (auto *structType = llvm::dyn_cast<llvm::StructType>(returnType)) {
                    const auto structName = structType->getName().str();
                    if (const auto structVal = generate_brace_init_for_struct(*braceInit, structType, structName)) {
                        builder->CreateRet(structVal);
                        return;
                    }
                }
            }
            auto val = generate_expression(*retStmt->value);
            llvm::Type *returnType = currentFunction->getReturnType();
            val = cast_value(val, returnType);
            builder->CreateRet(val);
        } else {
            builder->CreateRetVoid();
        }
    } else if (auto *exprStmt = dynamic_cast<const ExpressionStatement *>(&stmt)) {
        generate_expression(*exprStmt->expression);
    } else if (auto *ifStmt = dynamic_cast<const IfStatement *>(&stmt)) {
        generate_if_statement(*ifStmt);
    } else if (auto *forStmt = dynamic_cast<const ForStatement *>(&stmt)) {
        generate_for_statement(*forStmt);
    } else if (auto *whileStmt = dynamic_cast<const WhileStatement *>(&stmt)) {
        generate_while_statement(*whileStmt);
    } else if (auto *doWhileStmt = dynamic_cast<const DoWhileStatement *>(&stmt)) {
        generate_do_while_statement(*doWhileStmt);
    } else if (auto *switchStmt = dynamic_cast<const SwitchStatement *>(&stmt)) {
        generate_switch_statement(*switchStmt);
    } else if (auto *blockStmt = dynamic_cast<const Block *>(&stmt)) {
        generate_block(*blockStmt);
    } else if (dynamic_cast<const BreakStatement *>(&stmt)) {
        if (!breakTargets.empty()) {
            builder->CreateBr(breakTargets.back());
        }
    } else if (dynamic_cast<const ContinueStatement *>(&stmt)) {
        if (!continueTargets.empty()) {
            builder->CreateBr(continueTargets.back());
        }
    }
}

void Generator::generate_block(const Block &block) {
    push_scope();
    for (const auto &stmt: block.statements) {
        generate_statement(*stmt);
        if (builder->GetInsertBlock()->getTerminator()) {
            break;
        }
    }
    pop_scope();
}

void Generator::generate_if_statement(const IfStatement &stmt) {
    llvm::Value *condValue = generate_expression(*stmt.condition);

    if (!condValue->getType()->isIntegerTy(1)) {
        condValue = builder->CreateICmpNE(
            condValue,
            llvm::ConstantInt::get(condValue->getType(), 0),
            "ifcond"
        );
    }

    llvm::BasicBlock *thenBB = llvm::BasicBlock::Create(*context, "then", currentFunction);
    llvm::BasicBlock *elseBB = stmt.elseBranch
                                   ? llvm::BasicBlock::Create(*context, "else", currentFunction)
                                   : nullptr;
    llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create(*context, "ifcont", currentFunction);

    if (elseBB) {
        builder->CreateCondBr(condValue, thenBB, elseBB);
    } else {
        builder->CreateCondBr(condValue, thenBB, mergeBB);
    }

    builder->SetInsertPoint(thenBB);
    if (stmt.thenBranch) {
        generate_block(*stmt.thenBranch);
    }
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(mergeBB);
    }

    if (elseBB) {
        builder->SetInsertPoint(elseBB);
        if (stmt.elseBranch) {
            generate_block(*stmt.elseBranch);
        }
        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateBr(mergeBB);
        }
    }

    builder->SetInsertPoint(mergeBB);
}

void Generator::generate_for_statement(const ForStatement &stmt) {
    push_scope();

    if (stmt.initializer) {
        generate_expression(*stmt.initializer);
    }

    llvm::BasicBlock *condBB = llvm::BasicBlock::Create(*context, "for.cond", currentFunction);
    llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create(*context, "for.body", currentFunction);
    llvm::BasicBlock *incBB = llvm::BasicBlock::Create(*context, "for.inc", currentFunction);
    llvm::BasicBlock *endBB = llvm::BasicBlock::Create(*context, "for.end", currentFunction);

    breakTargets.push_back(endBB);
    continueTargets.push_back(incBB);

    builder->CreateBr(condBB);

    builder->SetInsertPoint(condBB);
    if (stmt.condition) {
        llvm::Value *condValue = generate_expression(*stmt.condition);
        if (!condValue->getType()->isIntegerTy(1)) {
            condValue = builder->CreateICmpNE(
                condValue,
                llvm::ConstantInt::get(condValue->getType(), 0),
                "forcond"
            );
        }
        builder->CreateCondBr(condValue, bodyBB, endBB);
    } else {
        builder->CreateBr(bodyBB);
    }

    builder->SetInsertPoint(bodyBB);
    if (stmt.body) {
        generate_block(*stmt.body);
    }
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(incBB);
    }

    builder->SetInsertPoint(incBB);
    if (stmt.postfix) {
        generate_expression(*stmt.postfix);
    }
    builder->CreateBr(condBB);

    builder->SetInsertPoint(endBB);

    breakTargets.pop_back();
    continueTargets.pop_back();

    pop_scope();
}

void Generator::generate_while_statement(const WhileStatement &stmt) {
    llvm::BasicBlock *condBB = llvm::BasicBlock::Create(*context, "while.cond", currentFunction);
    llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create(*context, "while.body", currentFunction);
    llvm::BasicBlock *endBB = llvm::BasicBlock::Create(*context, "while.end", currentFunction);

    breakTargets.push_back(endBB);
    continueTargets.push_back(condBB);

    builder->CreateBr(condBB);
    builder->SetInsertPoint(condBB);
    llvm::Value *condValue = generate_expression(*stmt.condition);
    if (!condValue->getType()->isIntegerTy(1)) {
        condValue = builder->CreateICmpNE(
            condValue,
            llvm::ConstantInt::get(condValue->getType(), 0),
            "whilecond"
        );
    }
    builder->CreateCondBr(condValue, bodyBB, endBB);
    builder->SetInsertPoint(bodyBB);

    if (stmt.body) {
        generate_block(*stmt.body);
    }
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(condBB);
    }

    builder->SetInsertPoint(endBB);

    breakTargets.pop_back();
    continueTargets.pop_back();
}

void Generator::generate_do_while_statement(const DoWhileStatement &stmt) {
    llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create(*context, "dowhile.body", currentFunction);
    llvm::BasicBlock *condBB = llvm::BasicBlock::Create(*context, "dowhile.cond", currentFunction);
    llvm::BasicBlock *endBB = llvm::BasicBlock::Create(*context, "dowhile.end", currentFunction);

    breakTargets.push_back(endBB);
    continueTargets.push_back(condBB);

    builder->CreateBr(bodyBB);
    builder->SetInsertPoint(bodyBB);
    if (stmt.body) {
        generate_block(*stmt.body);
    }
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(condBB);
    }

    builder->SetInsertPoint(condBB);
    llvm::Value *condValue = generate_expression(*stmt.condition);
    if (!condValue->getType()->isIntegerTy(1)) {
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

void Generator::generate_switch_statement(const SwitchStatement &stmt) {
    llvm::Value *switchValue = generate_expression(*stmt.value);

    llvm::BasicBlock *endBB = llvm::BasicBlock::Create(*context, "switch.end", currentFunction);
    llvm::BasicBlock *defaultBB = endBB;
    breakTargets.push_back(endBB);

    size_t numCases = 0;
    for (const auto &caseStmt: stmt.cases) {
        if (caseStmt->expression) {
            numCases++;
        } else {
            defaultBB = llvm::BasicBlock::Create(*context, "switch.default", currentFunction);
        }
    }

    llvm::SwitchInst *switchInst = builder->CreateSwitch(switchValue, defaultBB, numCases);

    for (const auto &caseStmt: stmt.cases) {
        llvm::BasicBlock *caseBB;

        if (caseStmt->expression) {
            caseBB = llvm::BasicBlock::Create(*context, "switch.case", currentFunction);
            llvm::Value *caseValue = generate_expression(*caseStmt->expression);

            // Cast case value to match switch value type
            if (caseValue->getType() != switchValue->getType()) {
                if (auto *caseConst = llvm::dyn_cast<llvm::ConstantInt>(caseValue)) {
                    caseValue = llvm::ConstantInt::get(switchValue->getType(),
                                                       caseConst->getSExtValue());
                }
            }

            if (auto *caseConst = llvm::dyn_cast<llvm::ConstantInt>(caseValue)) {
                switchInst->addCase(caseConst, caseBB);
            }
        } else {
            caseBB = defaultBB;
        }

        builder->SetInsertPoint(caseBB);
        if (caseStmt->body) {
            generate_block(*caseStmt->body);
        }
        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateBr(endBB);
        }
    }

    builder->SetInsertPoint(endBB);
    breakTargets.pop_back();
}

llvm::Value *Generator::generate_brace_init_for_struct(const BraceInitializer &braceInit, llvm::StructType *structType,
                                                       const std::string &structName) {
    auto *alloca = builder->CreateAlloca(structType, nullptr, "struct_init");
    builder->CreateStore(llvm::Constant::getNullValue(structType), alloca);

    const auto *fieldIndices = currentScope->get_field_indices(structName);
    if (!fieldIndices) {
        throw CompileError(DiagnosticCode::UNDEFINED_STRUCT, "struct não encontrada: " + structName);
    }

    for (size_t i = 0; i < braceInit.elements.size(); ++i) {
        const auto &elem = braceInit.elements[i];
        llvm::Value *val = generate_expression(*elem.value);
        if (!val) return nullptr;

        unsigned fieldIdx;
        if (elem.isDesignated()) {
            auto it = fieldIndices->find(elem.fieldName.token_name);
            if (it == fieldIndices->end()) {
                throw CompileError(DiagnosticCode::UNDEFINED_FIELD,
                                   "campo não encontrado: " + elem.fieldName.token_name);
            }
            fieldIdx = it->second;
        } else {
            fieldIdx = static_cast<unsigned>(i);
        }

        llvm::Type *fieldType = structType->getElementType(fieldIdx);
        val = cast_value(val, fieldType);

        auto *fieldPtr = builder->CreateStructGEP(structType, alloca, fieldIdx);
        builder->CreateStore(val, fieldPtr);
    }

    return builder->CreateLoad(structType, alloca, "struct_val");
}