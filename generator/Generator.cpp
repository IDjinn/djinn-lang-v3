//
// Created by Luke on 06/12/2025.
//

#include "Generator.h"

#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/CGSCCPassManager.h"

Generator::Generator(const std::string &module_name)
    : context(std::make_unique<llvm::LLVMContext>()),
      module(std::make_unique<llvm::Module>(module_name, *context)),
      builder(std::make_unique<llvm::IRBuilder<> >(*context)),
      currentScope(std::make_shared<GeneratorScope>()) {
}

void Generator::push_scope() {
    currentScope = std::make_shared<GeneratorScope>(currentScope);
}

void Generator::pop_scope() {
    if (currentScope->parent) {
        currentScope = currentScope->parent;
    }
}

void Generator::generate(const Program &program) {
    // ========================================================================
    // Multi-pass code generation (like C#/Roslyn)
    // This allows cyclic type references and order-independent declarations
    // ========================================================================

    // PASS 1: Forward declare ALL struct types as opaque
    // This must happen FIRST so all types are known before any usage
    for (const auto &ns: program.namespaces) {
        forward_declare_namespace_structs(*ns, "");
    }
    for (const auto &structDecl: program.structs) {
        forward_declare_struct(*structDecl);
    }

    // PASS 2: Process imports to create aliases for types
    // Now all types exist (as opaque), so aliases can be created
    for (const auto &import: program.imports) {
        const std::string nsPath = import->namespacePath.toString();

        // Cria aliases para todas as structs (regular, transparent, generic)
        for (const auto &[name, structDef]: currentScope->structs) {
            if (name.starts_with(nsPath + "::")) {
                const std::string shortName = name.substr(nsPath.length() + 2);
                if (shortName.find("::") == std::string::npos) {
                    if (!currentScope->has_struct_in_current_scope(shortName)) {
                        currentScope->define_alias(shortName, name);
                    }
                }
            }
        }
    }

    // PASS 3: External functions (C ABI)
    // Now type aliases exist, so extern functions can use types like c_result
    for (const auto &externFunc: program.externFunctions) {
        generate_extern_function(*externFunc);
    }

    // PASS 4: Generate enums
    for (const auto &enumDecl: program.enums) {
        generate_enum(*enumDecl, program.fileNamespace);
    }

    // PASS 5: Resolve struct bodies (fill in fields)
    // Now all types are known and extern functions declared
    for (const auto &ns: program.namespaces) {
        resolve_namespace_struct_bodies(*ns, "");
    }
    for (const auto &structDecl: program.structs) {
        resolve_struct_body(*structDecl);
    }

    // PASS 5: Generate struct methods
    // Bodies are complete, methods can use all types
    for (const auto &ns: program.namespaces) {
        generate_namespace_struct_methods(*ns, "");
    }
    for (const auto &structDecl: program.structs) {
        generate_struct_methods(*structDecl);
    }

    // PASS 6: Create function aliases from imports
    // Methods are generated, so function aliases can be created
    for (const auto &import: program.imports) {
        const std::string nsPath = import->namespacePath.toString();

        for (const auto &[name, func]: functions) {
            if (name.starts_with(nsPath + "::")) {
                const std::string shortName = name.substr(nsPath.length() + 2);
                if (shortName.find("::") == std::string::npos && !functions.contains(shortName)) {
                    functions[shortName] = func;
                }
            }
        }
    }

    // PASS 7: Generate namespace functions
    for (const auto &ns: program.namespaces) {
        generate_namespace_functions(*ns, "");
    }

    // PASS 8: Generate global functions
    for (const auto &func: program.functions) {
        generate_function(*func);
    }

    // PASS 9: Default main if not defined
    if (!functions.contains("main")) {
        generate_default_main();
    }
}

void Generator::generate_namespace(const NamespaceDeclaration &ns, const std::string &prefix) {
    const std::string qualifiedPrefix = prefix.empty() ? ns.name : prefix + "::" + ns.name;

    for (const auto &structDecl: ns.structs) {
        generate_struct(*structDecl, qualifiedPrefix);
    }

    for (const auto &func: ns.functions) {
        generate_function(*func, qualifiedPrefix);
    }

    for (const auto &nestedNs: ns.namespaces) {
        generate_namespace(*nestedNs, qualifiedPrefix);
    }
}

void Generator::generate_namespace_structs(const NamespaceDeclaration &ns, const std::string &prefix) {
    const std::string qualifiedPrefix = prefix.empty() ? ns.name : prefix + "::" + ns.name;

    for (const auto &structDecl: ns.structs) {
        generate_struct(*structDecl, qualifiedPrefix);
    }

    for (const auto &nestedNs: ns.namespaces) {
        generate_namespace_structs(*nestedNs, qualifiedPrefix);
    }
}

void Generator::generate_namespace_functions(const NamespaceDeclaration &ns, const std::string &prefix) {
    const std::string qualifiedPrefix = prefix.empty() ? ns.name : prefix + "::" + ns.name;

    for (const auto &func: ns.functions) {
        generate_function(*func, qualifiedPrefix);
    }

    for (const auto &nestedNs: ns.namespaces) {
        generate_namespace_functions(*nestedNs, qualifiedPrefix);
    }
}

void Generator::generate_default_main() {
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

llvm::Function *Generator::generate_function(
    const std::string &name,
    const Type &returnType,
    const std::vector<std::pair<Type, std::string> > &parameters
) {
    const auto return_value = generate_type(returnType);
    const auto llvmFunc = llvm::Function::Create(
        llvm::FunctionType::get(return_value, false),
        llvm::Function::ExternalLinkage,
        name,
        *module
    );
    functions[name] = llvmFunc;

    const auto entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
    builder->SetInsertPoint(entry);

    push_scope();
    size_t idx = 0;
    for (auto &arg: llvmFunc->args()) {
        const auto &param = parameters[idx];
        arg.setName(param.second);

        auto *alloca = builder->CreateAlloca(arg.getType(), nullptr, param.second);
        builder->CreateStore(&arg, alloca);
        std::string structTypeName = param.first.kind == TypeKind::STRUCT ? param.first.structName : "";
        currentScope->define_variable(param.second, alloca, structTypeName);
        idx++;
    }
    if (builder->GetInsertBlock()->getTerminator()) {
        pop_scope();
        return llvmFunc;
    }

    if (return_value->isVoidTy()) {
        builder->CreateRetVoid();
        pop_scope();
        return llvmFunc;
    }

    builder->CreateRet(llvm::Constant::getNullValue(return_value));
    pop_scope();
    return llvmFunc;
}

void Generator::optimize() const {
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

std::string Generator::print() const {
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