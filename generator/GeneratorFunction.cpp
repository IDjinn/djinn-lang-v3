//
// Created by Luke on 06/12/2025.
//

#include "Generator.h"


void Generator::forward_declare_function(const FunctionSymbol& func)
{
    llvm::Type* returnType = generate_type(func.returnType);

    std::vector<llvm::Type*> paramTypes;
    for (const auto& paramType : func.paramTypes)
    {
        paramTypes.push_back(generate_type(paramType));
    }

    const auto funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    const auto llvmFunc = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        func.name,
        *module
    );

    functions[func.name] = llvmFunc;
}

void Generator::generate_function_body(const FunctionSymbol& func)
{
    push_scope();

    llvm::Function* llvmFunc = functions[func.name];
    currentFunction = llvmFunc;

    const auto entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
    builder->SetInsertPoint(entry);

    size_t idx = 0;
    for (auto& arg : llvmFunc->args())
    {
        const auto& paramName = func.paramNames[idx];
        const auto& paramType = func.paramTypes[idx];
        arg.setName(paramName);

        auto* alloca = builder->CreateAlloca(arg.getType(), nullptr, paramName);
        builder->CreateStore(&arg, alloca);
        std::string structTypeName = paramType.kind == TypeKind::STRUCT ? paramType.structName : "";
        currentScope->define_variable(paramName, alloca, structTypeName);
        idx++;
    }

    if (func.body)
    {
        for (const auto& stmt : func.body->statements)
        {
            generate_statement(*stmt);
        }
    }

    if (builder->GetInsertBlock()->getTerminator())
    {
        pop_scope();
        return;
    }

    emit_scope_cleanup();

    llvm::Type* returnType = llvmFunc->getReturnType();
    if (returnType->isVoidTy())
    {
        builder->CreateRetVoid();
        pop_scope();
        return;
    }

    builder->CreateRet(llvm::Constant::getNullValue(returnType));
    pop_scope();
}

void Generator::generate_extern_function(const ExternFunctionSymbol& func)
{
    std::vector<llvm::Type*> paramTypes;
    for (const auto& paramType : func.paramTypes)
    {
        paramTypes.push_back(generate_type(paramType));
    }

    llvm::Type* returnType = generate_type(func.returnType);

    llvm::FunctionType* funcType = llvm::FunctionType::get(
        returnType,
        paramTypes,
        func.isVariadic
    );

    llvm::Function* llvmFunc = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        func.name,
        *module
    );

    functions[func.name] = llvmFunc;
    externFunctions.push_back(llvmFunc);

    if (func.abi == "C")
    {
        llvmFunc->setCallingConv(llvm::CallingConv::C);
    }
}

void Generator::emit_used_declarations()
{
    auto* i8PtrTy = llvm::PointerType::getUnqual(*context);
    std::vector<llvm::Constant*> usedItems;

    for (auto* func : externFunctions)
    {
        usedItems.push_back(llvm::ConstantExpr::getBitCast(func, i8PtrTy));
    }

    int typeIdx = 0;
    for (auto* structType : declaredTypes)
    {
        if (structType->isOpaque()) continue;

        auto* dummy = new llvm::GlobalVariable(
            *module,
            structType,
            false,
            llvm::GlobalValue::ExternalLinkage,
            llvm::Constant::getNullValue(structType),
            "__djinn_type_" + std::to_string(typeIdx++)
        );
        dummy->setVisibility(llvm::GlobalValue::HiddenVisibility);
        usedItems.push_back(llvm::ConstantExpr::getBitCast(dummy, i8PtrTy));
    }

    if (usedItems.empty()) return;

    auto* arrayTy = llvm::ArrayType::get(i8PtrTy, usedItems.size());
    auto* usedArray = llvm::ConstantArray::get(arrayTy, usedItems);

    auto* gv = new llvm::GlobalVariable(
        *module,
        arrayTy,
        false,
        llvm::GlobalValue::AppendingLinkage,
        usedArray,
        "llvm.compiler.used"
    );
    gv->setSection("llvm.metadata");
}