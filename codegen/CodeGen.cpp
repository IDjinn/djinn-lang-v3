//
// Created by Luke on 06/12/2025.
//

#include "CodeGen.h"

#include "util.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/CGSCCPassManager.h"

CodeGen::CodeGen()
    : context(std::make_unique<llvm::LLVMContext>()),
      module(std::make_unique<llvm::Module>("djinn", *context)),
      builder(std::make_unique<llvm::IRBuilder<> >(*context)) {
    declare_extern_functions();
}

void CodeGen::declare_extern_functions() {
    functions["printf"] = llvm::Function::Create(
        llvm::FunctionType::get(
            builder->getInt32Ty(),
            {builder->getPtrTy()},
            true // varargs
        ),
        llvm::Function::ExternalLinkage,
        "printf",
        *module
    );
}

void CodeGen::generate(const Program &program) {
    for (const auto &structDecl: program.structs) {
        generate_struct(*structDecl);
    }

    for (const auto &func: program.functions) {
        generate_function(*func);
    }

    if (!functions.contains("main")) {
        generate_default_main();
    }
}

void CodeGen::generate_struct(const StructDeclaration &structDecl) {
    std::vector<llvm::Type *> fieldTypes;
    std::unordered_map<std::string, unsigned> fieldIndices;

    unsigned idx = 0;
    for (const auto &field: structDecl.fields) {
        fieldTypes.push_back(generate_type(*field.type));
        fieldIndices[field.name] = idx++;
    }

    llvm::StructType *structType = llvm::StructType::create(
        *context,
        fieldTypes,
        structDecl.name
    );

    structTypes[structDecl.name] = structType;
    structFieldIndices[structDecl.name] = std::move(fieldIndices);
}

void CodeGen::generate_default_main() {
    const auto mainFunc = llvm::Function::Create(
        llvm::FunctionType::get(builder->getInt32Ty(), false),
        llvm::Function::ExternalLinkage,
        "main",
        *module
    );
    functions["main"] = mainFunc;

    const auto entry = llvm::BasicBlock::Create(*context, "entry", mainFunc);
    builder->SetInsertPoint(entry);
    builder->CreateRet(builder->getInt32(0));
}

void CodeGen::generate_function(const FunctionDeclaration &func) {
    namedValues.clear();
    variableStructTypes.clear();

    llvm::Type *returnType = this->generate_type(*func.returnType);

    std::vector<llvm::Type *> paramTypes{};
    for (const auto &param: func.parameters) {
        paramTypes.emplace_back(generate_type(*param.type));
    }

    const auto funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    const auto llvmFunc = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        func.name,
        *module
    );
    functions[func.name] = llvmFunc;
    currentFunction = llvmFunc;

    const auto entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
    builder->SetInsertPoint(entry);

    size_t idx = 0;
    for (auto &arg: llvmFunc->args()) {
        const auto &param = func.parameters[idx];
        arg.setName(param.name);

        auto *alloca = builder->CreateAlloca(arg.getType(), nullptr, param.name);
        builder->CreateStore(&arg, alloca);
        namedValues[param.name] = alloca;
        if (param.type->kind == TypeKind::STRUCT) {
            variableStructTypes[param.name] = param.type->structName;
        }
        idx++;
    }

    if (func.body) {
        for (const auto &stmt: func.body->statements) {
            generate_statement(*stmt);
        }
    }

    if (builder->GetInsertBlock()->getTerminator()) return;

    if (returnType->isVoidTy()) {
        builder->CreateRetVoid();
        return;
    }

    builder->CreateRet(llvm::Constant::getNullValue(returnType));
}

llvm::Type *CodeGen::generate_type(Type &type) {
    switch (type.kind) {
        case TypeKind::INTEGER: return builder->getIntNTy(type.size);
        case TypeKind::STRING: return builder->getPtrTy();
        case TypeKind::VOID: return builder->getVoidTy();
        case TypeKind::F16: return builder->getHalfTy();
        case TypeKind::F32: return builder->getFloatTy();
        case TypeKind::F64: return builder->getDoubleTy();
        case TypeKind::F128: return llvm::Type::getFP128Ty(*context);
        case TypeKind::ARRAY: {
            if (!type.elementType) {
                throw CompileError(DiagnosticCode::INVALID_TYPE, "tipo array deve ter tipo de elemento");
            }
            llvm::Type *elemType = generate_type(*type.elementType);
            return llvm::PointerType::get(elemType, 0);
        }
        case TypeKind::POINTER: {
            if (!type.elementType) {
                throw CompileError(DiagnosticCode::INVALID_TYPE, "tipo ponteiro deve ter tipo de elemento");
            }
            llvm::Type *pointeeType = generate_type(*type.elementType);
            return llvm::PointerType::get(pointeeType, 0);
        }
        case TypeKind::STRUCT: {
            auto it = structTypes.find(type.structName);
            if (it == structTypes.end()) {
                throw CompileError(DiagnosticCode::UNDEFINED_STRUCT, "struct não encontrada: " + type.structName);
            }
            return it->second;
        }
        case TypeKind::AUTO:
            throw CompileError(DiagnosticCode::INVALID_TYPE, "tipo auto deve ser inferido antes da geração de código");
        default: throw CompileError(DiagnosticCode::INVALID_TYPE, "tipo inválido");
    }
}

void CodeGen::generate_statement(const Statement &stmt) {
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
            const auto val = generate_expression(*retStmt->value);
            builder->CreateRet(val);
        } else {
            builder->CreateRetVoid();
        }
    } else if (auto *exprStmt = dynamic_cast<const ExpressionStatement *>(&stmt)) {
        generate_expression(*exprStmt->expression);
    }
}

llvm::Value *CodeGen::generate_brace_init_for_struct(const BraceInitializer &braceInit, llvm::StructType *structType,
                                                     const std::string &structName) {
    auto *alloca = builder->CreateAlloca(structType, nullptr, "struct_init");
    builder->CreateStore(llvm::Constant::getNullValue(structType), alloca);

    const auto &fieldIndices = structFieldIndices[structName];
    for (size_t i = 0; i < braceInit.elements.size(); ++i) {
        const auto &elem = braceInit.elements[i];
        llvm::Value *val = generate_expression(*elem.value);
        if (!val) return nullptr;

        unsigned fieldIdx;
        if (elem.isDesignated()) {
            // Designated initializer: .field = value
            auto it = fieldIndices.find(elem.fieldName);
            if (it == fieldIndices.end()) {
                throw CompileError(DiagnosticCode::UNDEFINED_FIELD, "campo não encontrado: " + elem.fieldName);
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

llvm::Value *CodeGen::generate_expression(const Expression &expr) {
    if (auto *intLit = dynamic_cast<const IntegerLiteral *>(&expr)) {
        const llvm::APInt apVal(128, intLit->value, 10);
        const unsigned activeBits = apVal.getActiveBits();

        unsigned bits;
        // if (activeBits <= 1) {
        //     bits = 1;
        // } else
        if (activeBits <= 8) {
            bits = 8;
        } else if (activeBits <= 16) {
            bits = 16;
        } else if (activeBits <= 32) {
            bits = 32;
        } else if (activeBits <= 64) {
            bits = 64;
        } else {
            bits = 128;
        }

        return llvm::ConstantInt::get(builder->getIntNTy(bits), apVal.trunc(bits));
    }

    if (auto *strLit = dynamic_cast<const StringLiteral *>(&expr)) {
        return builder->CreateGlobalString(strLit->value);
    }

    if (auto *binExpr = dynamic_cast<const BinaryExpression *>(&expr)) {
        auto *left = generate_expression(*binExpr->left);
        auto *right = generate_expression(*binExpr->right);

        if (!left || !right) return nullptr; // TODO: assert & compiler error

        switch (binExpr->op) {
            case TokenType::PLUS:
                return builder->CreateAdd(left, right, "addtmp");
            case TokenType::MINUS:
                return builder->CreateSub(left, right, "subtmp");
            case TokenType::STAR:
                return builder->CreateMul(left, right, "multmp");
            case TokenType::SLASH:
                return builder->CreateSDiv(left, right, "divtmp");
            case TokenType::PERCENT:
                return builder->CreateSRem(left, right, "modtmp");

            case TokenType::EQUAL_EQUAL:
                return builder->CreateICmpEQ(left, right, "eqtmp");
            case TokenType::BANG_EQUAL:
                return builder->CreateICmpNE(left, right, "netmp");
            case TokenType::LESS:
                return builder->CreateICmpSLT(left, right, "lttmp");
            case TokenType::LESS_EQUAL:
                return builder->CreateICmpSLE(left, right, "letmp");
            case TokenType::GREATER:
                return builder->CreateICmpSGT(left, right, "gttmp");
            case TokenType::GREATER_EQUAL:
                return builder->CreateICmpSGE(left, right, "getmp");

            case TokenType::AND_AND:
                return builder->CreateAnd(left, right, "andtmp");
            case TokenType::OR_OR:
                return builder->CreateOr(left, right, "ortmp");

            default:
                throw CompileError(DiagnosticCode::UNSUPPORTED_OPERATOR, "operador binário não suportado");
        }
    }

    if (auto *unaryExpr = dynamic_cast<const UnaryExpression *>(&expr)) {
        auto *operand = generate_expression(*unaryExpr->operand);
        if (!operand) return nullptr;

        switch (unaryExpr->op) {
            case TokenType::MINUS:
                return builder->CreateNeg(operand, "negtmp");
            case TokenType::BANG:
                return builder->CreateNot(operand, "nottmp");
            default:
                throw CompileError(DiagnosticCode::UNSUPPORTED_OPERATOR, "operador unário não suportado");
        }
    }

    if (auto *call = dynamic_cast<const FunctionCall *>(&expr)) {
        const auto it = functions.find(call->name);
        if (it == functions.end()) {
            throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION, "função não encontrada: " + call->name);
        }

        llvm::Function *func = it->second;
        llvm::FunctionType *funcType = func->getFunctionType();

        std::vector<llvm::Value *> args;
        size_t argIdx = 0;
        for (const auto &arg: call->arguments) {
            llvm::Value *argVal = generate_expression(*arg);

            if (argIdx < funcType->getNumParams()) {
                argVal = cast_value(argVal, funcType->getParamType(argIdx));
            } else if (funcType->isVarArg()) {
                if (argVal->getType()->isIntegerTy() &&
                    argVal->getType()->getIntegerBitWidth() < 32) {
                    argVal = builder->CreateSExt(argVal, builder->getInt32Ty(), "vararg_promote");
                }
            }
            const auto value = llvm::to_string(*argVal);
            args.push_back(argVal);
            argIdx++;
        }

        return builder->CreateCall(func, args);
    }

    if (auto *ident = dynamic_cast<const Identifier *>(&expr)) {
        auto it = namedValues.find(ident->name);
        if (it != namedValues.end()) {
            return builder->CreateLoad(it->second->getAllocatedType(), it->second, ident->name);
        }
        throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE, "variável não encontrada: " + ident->name);
    }

    if (auto *varDecl = dynamic_cast<const VariableDeclaration *>(&expr)) {
        llvm::Type *type = generate_type(const_cast<Type &>(varDecl->type));
        auto *alloca = builder->CreateAlloca(type, nullptr, varDecl->name);
        // Initialize with default value (0)
        builder->CreateStore(llvm::Constant::getNullValue(type), alloca);
        namedValues[varDecl->name] = alloca;
        if (varDecl->type.kind == TypeKind::STRUCT) {
            variableStructTypes[varDecl->name] = varDecl->type.structName;
        }
        return alloca;
    }

    if (auto *fieldAccess = dynamic_cast<const FieldAccess *>(&expr)) {
        if (auto *ident = dynamic_cast<const Identifier *>(fieldAccess->object.get())) {
            auto allocaIt = namedValues.find(ident->name);
            if (allocaIt == namedValues.end()) {
                throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE, "variável não encontrada: " + ident->name);
            }

            std::string structName;
            llvm::StructType *structType = nullptr;

            if (auto structTypeIt = variableStructTypes.find(ident->name); structTypeIt != variableStructTypes.end()) {
                structName = structTypeIt->second;
                structType = structTypes[structName];
            } else {
                const auto allocatedType = allocaIt->second->getAllocatedType();
                if (auto *llvmStructType = llvm::dyn_cast<llvm::StructType>(allocatedType)) {
                    structType = llvmStructType;
                    structName = llvmStructType->getName().str();
                } else {
                    throw CompileError(DiagnosticCode::NOT_A_STRUCT, "variável não é uma struct: " + ident->name);
                }
            }

            const auto &fieldIndices = structFieldIndices[structName];

            auto fieldIt = fieldIndices.find(fieldAccess->fieldName);
            if (fieldIt == fieldIndices.end()) {
                throw CompileError(DiagnosticCode::UNDEFINED_FIELD, "campo não encontrado: " + fieldAccess->fieldName);
            }

            unsigned fieldIdx = fieldIt->second;
            auto *fieldPtr = builder->CreateStructGEP(structType, allocaIt->second, fieldIdx,
                                                      fieldAccess->fieldName + "_ptr");
            llvm::Type *fieldType = structType->getElementType(fieldIdx);
            return builder->CreateLoad(fieldType, fieldPtr, fieldAccess->fieldName);
        }

        throw CompileError(DiagnosticCode::INVALID_OPERATION, "acesso a campo só suportado em identificadores simples");
    }

    if (auto *varInit = dynamic_cast<const VariableInit *>(&expr)) {
        // Check if initializing with a BraceInitializer
        if (auto *braceInit = dynamic_cast<const BraceInitializer *>(varInit->value.get())) {
            // Handle struct initialization with brace initializer
            if (varInit->type.kind == TypeKind::STRUCT) {
                llvm::StructType *structType = structTypes[varInit->type.structName];
                if (!structType) {
                    throw CompileError(DiagnosticCode::UNDEFINED_STRUCT,
                                       "struct não encontrada: " + varInit->type.structName);
                }

                auto *alloca = builder->CreateAlloca(structType, nullptr, varInit->name);
                namedValues[varInit->name] = alloca;
                variableStructTypes[varInit->name] = varInit->type.structName;

                const auto &fieldIndices = structFieldIndices[varInit->type.structName];

                for (size_t i = 0; i < braceInit->elements.size(); ++i) {
                    const auto &elem = braceInit->elements[i];
                    llvm::Value *val = generate_expression(*elem.value);
                    if (!val) return nullptr;

                    unsigned fieldIdx;
                    if (elem.isDesignated()) {
                        // Designated initializer: .field = value
                        auto it = fieldIndices.find(elem.fieldName);
                        if (it == fieldIndices.end()) {
                            throw CompileError(DiagnosticCode::UNDEFINED_FIELD, "campo não encontrado: " + elem.fieldName);
                        }
                        fieldIdx = it->second;
                    } else {
                        // Positional initializer
                        fieldIdx = static_cast<unsigned>(i);
                    }

                    llvm::Type *fieldType = structType->getElementType(fieldIdx);
                    val = cast_value(val, fieldType);

                    auto *fieldPtr = builder->CreateStructGEP(structType, alloca, fieldIdx);
                    builder->CreateStore(val, fieldPtr);
                }

                return alloca;
            }

            // For non-struct types with single value initializer: i32 x = { 10 }
            if (braceInit->elements.size() == 1 && !braceInit->elements[0].isDesignated()) {
                llvm::Value *initVal = generate_expression(*braceInit->elements[0].value);
                if (!initVal) {
                    throw CompileError(DiagnosticCode::INVALID_OPERATION, "não foi possível gerar valor inicial para: " + varInit->name);
                }

                llvm::Type *type = generate_type(const_cast<Type &>(varInit->type));
                initVal = cast_value(initVal, type);

                auto *alloca = builder->CreateAlloca(type, nullptr, varInit->name);
                namedValues[varInit->name] = alloca;
                builder->CreateStore(initVal, alloca);
                return alloca;
            }
        }

        // Regular initialization (not brace initializer)
        llvm::Value *initVal = generate_expression(*varInit->value);
        if (!initVal) {
            throw CompileError(DiagnosticCode::INVALID_OPERATION, "não foi possível gerar valor inicial para: " + varInit->name);
        }

        llvm::Type *type;
        if (varInit->type.kind == TypeKind::AUTO) {
            // Inferência de tipo: usa o tipo do valor
            type = initVal->getType();
        } else {
            type = generate_type(const_cast<Type &>(varInit->type));
            initVal = cast_value(initVal, type);
        }

        auto *alloca = builder->CreateAlloca(type, nullptr, varInit->name);
        namedValues[varInit->name] = alloca;
        builder->CreateStore(initVal, alloca);
        return alloca;
    }

    if (auto *assign = dynamic_cast<const Assignment *>(&expr)) {
        auto it = namedValues.find(assign->name);
        if (it == namedValues.end()) {
            throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE, "variável não encontrada: " + assign->name);
        }

        llvm::Value *val = generate_expression(*assign->value);
        if (val) {
            val = cast_value(val, it->second->getAllocatedType());
            builder->CreateStore(val, it->second);
        }
        return val;
    }

    if (auto *braceInit = dynamic_cast<const BraceInitializer *>(&expr)) {
        // BraceInitializer creates a struct value
        // For now, we need context to know the target type
        // This is typically used in VariableInit, so we return an aggregate

        if (braceInit->elements.empty()) {
            return nullptr;
        }

        // Check if all elements are designated (struct init) or positional
        bool hasDesignated = false;
        bool hasPositional = false;
        for (const auto &elem: braceInit->elements) {
            if (elem.isDesignated()) {
                hasDesignated = true;
            } else {
                hasPositional = true;
            }
        }

        if (hasDesignated && hasPositional) {
            throw CompileError(DiagnosticCode::MIXED_INITIALIZERS,
                               "não é possível misturar inicializadores designados e posicionais");
        }

        // Generate values for all elements
        std::vector<llvm::Value *> values;
        for (const auto &elem: braceInit->elements) {
            llvm::Value *val = generate_expression(*elem.value);
            if (!val) return nullptr;
            values.push_back(val);
        }

        // If there's only one value and it's not designated, return it directly
        if (values.size() == 1 && !hasDesignated) {
            return values[0];
        }

        // For multiple values, we need a target struct type
        // This will be handled by VariableInit which knows the target type
        // For now, return the first value (aggregate handling requires more context)
        return values[0];
    }

    return nullptr;
}

void CodeGen::optimize() {
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB;
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
    MPM.run(*module, MAM);
}

std::string CodeGen::print() const {
    std::string errorStr;
    llvm::raw_string_ostream errorStream(errorStr);

    if (llvm::verifyModule(*module, &errorStream)) {
        return "Erro: módulo inválido\n" + errorStr;
    }

    std::string str;
    llvm::raw_string_ostream stream(str);
    module->print(stream, nullptr);
    return str;
}

llvm::Value *CodeGen::cast_value(llvm::Value *value, llvm::Type *targetType) {
    if (!value || !targetType) return value;

    llvm::Type *srcType = value->getType();
    if (srcType == targetType) return value;

    if (srcType->isIntegerTy() && targetType->isIntegerTy()) {
        unsigned srcBits = srcType->getIntegerBitWidth();
        unsigned dstBits = targetType->getIntegerBitWidth();

        if (srcBits < dstBits) {
            return builder->CreateSExt(value, targetType, "sext");
        } else if (srcBits > dstBits) {
            return builder->CreateTrunc(value, targetType, "trunc");
        }
    }

    if (srcType->isFloatingPointTy() && targetType->isFloatingPointTy()) {
        if (srcType->getPrimitiveSizeInBits() < targetType->getPrimitiveSizeInBits()) {
            return builder->CreateFPExt(value, targetType, "fpext");
        } else {
            return builder->CreateFPTrunc(value, targetType, "fptrunc");
        }
    }

    if (srcType->isIntegerTy() && targetType->isFloatingPointTy()) {
        return builder->CreateSIToFP(value, targetType, "sitofp");
    }

    if (srcType->isFloatingPointTy() && targetType->isIntegerTy()) {
        return builder->CreateFPToSI(value, targetType, "fptosi");
    }

    return value;
}
