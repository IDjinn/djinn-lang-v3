//
// Created by Luke on 06/12/2025.
//

#include "Generator.h"

llvm::Value *Generator::generate_expression(const Expression &expr) {
    if (auto *intLit = dynamic_cast<const IntegerLiteral *>(&expr)) {
        return generate_integer_literal(*intLit);
    }

    if (auto *floatLit = dynamic_cast<const FloatLiteral *>(&expr)) {
        return generate_float_literal(*floatLit);
    }

    if (auto *strLit = dynamic_cast<const StringLiteral *>(&expr)) {
        return generate_string_literal(*strLit);
    }

    if (auto *binExpr = dynamic_cast<const BinaryExpression *>(&expr)) {
        return generate_binary_expression(*binExpr);
    }

    if (auto *unaryExpr = dynamic_cast<const UnaryExpression *>(&expr)) {
        return generate_unary_expression(*unaryExpr);
    }

    if (auto *call = dynamic_cast<const FunctionCall *>(&expr)) {
        return generate_function_call(*call);
    }

    if (auto *ident = dynamic_cast<const Identifier *>(&expr)) {
        return generate_identifier(*ident);
    }

    if (auto *varDecl = dynamic_cast<const VariableDeclaration *>(&expr)) {
        return generate_variable_declaration(*varDecl);
    }

    if (auto *fieldAccess = dynamic_cast<const FieldAccess *>(&expr)) {
        return generate_field_access(*fieldAccess);
    }

    if (auto *fieldAssign = dynamic_cast<const FieldAssignment *>(&expr)) {
        return generate_field_assignment(*fieldAssign);
    }

    if (auto *varInit = dynamic_cast<const VariableInit *>(&expr)) {
        return generate_variable_init(*varInit);
    }

    if (auto *assign = dynamic_cast<const Assignment *>(&expr)) {
        return generate_assignment(*assign);
    }

    if (auto *braceInit = dynamic_cast<const BraceInitializer *>(&expr)) {
        return generate_brace_initializer(*braceInit);
    }

    if (auto *switchExpr = dynamic_cast<const SwitchExpression *>(&expr)) {
        return generate_switch_expression(*switchExpr);
    }

    if (dynamic_cast<const VariadicForward *>(&expr)) {
        // VariadicForward should not be evaluated directly - it's handled by FunctionCall
        throw CompileError(DiagnosticCode::UNEXPECTED_TOKEN,
                           "variadic forwarding '...' can only be used as function argument");
    }

    return nullptr;
}

// ============================================================================
// Switch Expression Generation - Pattern Matching on Enums
// ============================================================================
//
// For: switch opt { Value val -> val, Empty -> -1 }
//
// 1. Extract the tag from the enum
// 2. Switch on the tag
// 3. For each arm, create a block that:
//    - Extracts payload if there's a binding
//    - Evaluates the result expression
//    - Branches to merge
// 4. Merge with phi node to collect results
// ============================================================================
llvm::Value *Generator::generate_switch_expression(const SwitchExpression &expr) {
    // Generate the value being matched
    llvm::Value *enumValue = generate_expression(*expr.value);
    if (!enumValue) {
        GENERATOR_ERROR(DiagnosticCode::UNEXPECTED_TOKEN, "Failed to generate switch expression value",
                        expr.value->location);
    }

    // Determine the enum type from the value
    // The value should be an enum type
    llvm::Type *enumType = enumValue->getType();
    if (!enumType->isStructTy()) {
        GENERATOR_ERROR(DiagnosticCode::UNEXPECTED_TOKEN, "Switch expression value must be an enum type",
                        expr.value->location);
    }

    // Find the enum definition
    std::string enumTypeName = enumType->getStructName().str();
    EnumDef *enumDef = currentScope->lookup_enum(enumTypeName);
    if (!enumDef) {
        GENERATOR_ERROR(DiagnosticCode::UNEXPECTED_TOKEN, "Unknown enum type: " + enumTypeName,
                        expr.value->location);
    }

    // Extract the tag
    llvm::Value *tag = extract_enum_tag(enumValue, *enumDef);

    // Create blocks for each arm and the merge block
    llvm::Function *func = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock *mergeBlock = llvm::BasicBlock::Create(*context, "switch_merge", func);
    llvm::BasicBlock *defaultBlock = mergeBlock; // Default goes to merge if no default arm

    std::vector<std::pair<llvm::BasicBlock *, llvm::Value *> > armResults;

    // Create switch instruction
    llvm::SwitchInst *switchInst = builder->CreateSwitch(tag, defaultBlock, expr.arms.size());

    // Process each arm
    for (const auto &arm: expr.arms) {
        // Find the variant
        const EnumVariantDef *variant = enumDef->getVariant(arm.variantName.token_name);
        if (!variant) {
            GENERATOR_ERROR(DiagnosticCode::UNEXPECTED_TOKEN,
                            "Unknown variant: " + arm.variantName.token_name,
                            arm.variantName.location);
        }

        // Create block for this arm
        llvm::BasicBlock *armBlock = llvm::BasicBlock::Create(
            *context, "case_" + arm.variantName.token_name, func);

        // Add case to switch
        llvm::ConstantInt *tagConstant = llvm::ConstantInt::get(
            static_cast<llvm::IntegerType *>(enumDef->tagType), variant->tag);
        switchInst->addCase(tagConstant, armBlock);

        // Generate arm body
        builder->SetInsertPoint(armBlock);

        push_scope();

        // If there's a binding, extract the payload and define the variable
        if (arm.binding) {
            size_t variantIdx = 0;
            for (size_t i = 0; i < enumDef->variants.size(); ++i) {
                if (enumDef->variants[i].name.token_name == arm.variantName.token_name) {
                    variantIdx = i;
                    break;
                }
            }

            llvm::Value *payload = extract_enum_payload(enumValue, *enumDef, variantIdx);
            if (payload) {
                // Create alloca for the binding
                llvm::AllocaInst *bindingAlloca = builder->CreateAlloca(
                    payload->getType(), nullptr, arm.binding->token_name);
                builder->CreateStore(payload, bindingAlloca);
                currentScope->define_variable(arm.binding->token_name, bindingAlloca);
            }
        }

        // Generate the result expression
        llvm::Value *result = generate_expression(*arm.result);

        pop_scope();

        // Store result and current block (for phi)
        llvm::BasicBlock *currentBlock = builder->GetInsertBlock();
        armResults.emplace_back(currentBlock, result);

        // Branch to merge
        builder->CreateBr(mergeBlock);
    }

    // Generate merge block with phi
    builder->SetInsertPoint(mergeBlock);

    if (armResults.empty()) {
        GENERATOR_ERROR(DiagnosticCode::UNEXPECTED_TOKEN, "Switch expression must have at least one arm",
                        expr.location);
    }

    // Create phi node to merge results
    llvm::Type *resultType = armResults[0].second->getType();
    llvm::PHINode *phi = builder->CreatePHI(resultType, armResults.size(), "switch_result");

    for (const auto &[block, value]: armResults) {
        phi->addIncoming(value, block);
    }

    return phi;
}