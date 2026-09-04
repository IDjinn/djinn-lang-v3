//
// Native-exceptions lowering (--exceptions): throwing calls become invokes
// and unwinding lands on the innermost active landing (see Generator.h). One
// C++ type — djinn::error — crosses the boundary via the runtime shim; the
// shim mirrors the error into the thread-local error state before throwing,
// so handlers re-enter normal blocks that read it like the errno mode does.
//
// Windows (MSVC ABI) is lowered with funclet EH (catchswitch/catchpad/
// cleanuppad + __CxxFrameHandler3); other targets are not enabled yet.
//

#include "Generator.h"
#include "../utils/Logger.h"

#include "llvm/IR/Intrinsics.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"

void Generator::setup_target_triple()
{
    if (!module->getTargetTriple().empty()) return;

    const llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
    module->setTargetTriple(triple);

    std::string error;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple.str(), error);
    if (!target)
    {
        LOG_WARN("[generator] cannot resolve target '%s': %s", triple.str().c_str(), error.c_str());
        return;
    }
    const std::unique_ptr<llvm::TargetMachine> machine(
        target->createTargetMachine(triple, "", "", llvm::TargetOptions(), std::nullopt, std::nullopt));
    if (machine)
    {
        module->setDataLayout(machine->createDataLayout());
    }
}

bool Generator::eh_is_msvc_target() const
{
    return module->getTargetTriple().isWindowsMSVCEnvironment();
}

// Personality + the djinn::error RTTI type descriptor the catchpads match
// against. The descriptor symbol lives in the exceptions shim
// (runtime/djinn_exceptions.cpp) and its mangled name is fixed by the MSVC
// ABI: ??_R0?AVerror@djinn@@@8.
void Generator::ensure_eh_declarations()
{
    if (ehPersonalityFn) return;

    auto* ptrTy = builder->getPtrTy();

    ehPersonalityFn = module->getFunction("__CxxFrameHandler3");
    if (!ehPersonalityFn)
    {
        auto* personalityTy = llvm::FunctionType::get(builder->getInt32Ty(), true);
        ehPersonalityFn = llvm::Function::Create(personalityTy, llvm::Function::ExternalLinkage,
                                                 "__CxxFrameHandler3", *module);
    }

    llvm::GlobalVariable* desc = module->getNamedGlobal("??_R0?AVerror@djinn@@@8");
    if (!desc)
    {
        desc = new llvm::GlobalVariable(*module, ptrTy, false,
                                        llvm::GlobalVariable::ExternalLinkage, nullptr,
                                        "??_R0?AVerror@djinn@@@8");
    }
    ehErrorTypeDesc = desc;
}

// Creates the (still unterminated) landing blocks and makes them current.
Generator::NativeLanding Generator::push_native_landing(const bool cleanupOnly)
{
    ensure_eh_declarations();
    if (currentFunction && !currentFunction->hasPersonalityFn())
    {
        currentFunction->setPersonalityFn(ehPersonalityFn);
    }

    NativeLanding landing;
    landing.cleanupOnly = cleanupOnly;
    landing.dispatchBB = llvm::BasicBlock::Create(*context, "eh.dispatch", currentFunction);
    if (!cleanupOnly)
    {
        landing.djinnPad = llvm::BasicBlock::Create(*context, "eh.catch.djinn", currentFunction);
        landing.allPad = llvm::BasicBlock::Create(*context, "eh.catch.foreign", currentFunction);
    }
    ehLandingStack_.push_back(landing);
    return landing;
}

void Generator::finalize_native_landing(const NativeLanding& landing, llvm::BasicBlock* handlerBB)
{
    if (landing.cleanupOnly)
    {
        builder->SetInsertPoint(landing.dispatchBB);
        auto* pad = builder->CreateCleanupPad(llvm::ConstantTokenNone::get(*context));
        emit_all_scope_cleanup();
        builder->CreateCleanupRet(pad, nullptr);
        return;
    }

    builder->SetInsertPoint(landing.dispatchBB);
    auto* catchSwitch = builder->CreateCatchSwitch(
        llvm::ConstantTokenNone::get(*context), nullptr, 2);
    catchSwitch->addHandler(landing.djinnPad);
    catchSwitch->addHandler(landing.allPad);

    // djinn::error: handlers re-read the thread-local error state (already
    // mirrored by __djinn_throw), so the pad only resumes the parent at the
    // handler block.
    builder->SetInsertPoint(landing.djinnPad);
    auto* djinnPad = builder->CreateCatchPad(catchSwitch, {ehErrorTypeDesc});
    builder->CreateCatchRet(djinnPad, handlerBB);

    // Foreign exception: wrap as ForeignError (also mirrored into the error
    // state by the shim) and resume at the same handler.
    builder->SetInsertPoint(landing.allPad);
    auto* foreignPad = builder->CreateCatchPad(catchSwitch,
                                               {llvm::ConstantPointerNull::get(builder->getPtrTy())});
    auto* wrapTy = llvm::FunctionType::get(builder->getPtrTy(), false);
    auto* wrapFn = module->getFunction("__djinn_wrap_foreign");
    if (!wrapFn)
    {
        wrapFn = llvm::Function::Create(wrapTy, llvm::Function::ExternalLinkage,
                                        "__djinn_wrap_foreign", *module);
    }
    builder->CreateCall(wrapFn);
    builder->CreateCatchRet(foreignPad, handlerBB);
}

llvm::CallBase* Generator::emit_call_or_invoke(llvm::Function* callee, const std::vector<llvm::Value*>& args,
                                               const bool calleeCanThrow)
{
    if (!nativeExceptions || !calleeCanThrow)
    {
        return builder->CreateCall(callee, args);
    }

    ensure_eh_declarations();
    if (currentFunction && !currentFunction->hasPersonalityFn())
    {
        currentFunction->setPersonalityFn(ehPersonalityFn);
    }

    if (!ehLandingStack_.empty())
    {
        const auto& landing = ehLandingStack_.back();
        auto* okBB = llvm::BasicBlock::Create(*context, "invoke.ok", currentFunction);
        auto* invoke = builder->CreateInvoke(callee, okBB, landing.dispatchBB, args);
        builder->SetInsertPoint(okBB);
        return invoke;
    }

    // Unchecked call in a throwing function: run the scope cleanup and keep
    // unwinding (native propagation).
    const auto landing = push_native_landing(true);
    auto* okBB = llvm::BasicBlock::Create(*context, "invoke.ok", currentFunction);
    auto* invoke = builder->CreateInvoke(callee, okBB, landing.dispatchBB, args);
    builder->SetInsertPoint(okBB);
    finalize_native_landing(landing, nullptr);
    ehLandingStack_.pop_back();
    return invoke;
}

void Generator::emit_native_throw(llvm::Value* tag, llvm::Value* message, llvm::Value* typeName)
{
    ensure_eh_declarations();
    if (currentFunction && !currentFunction->hasPersonalityFn())
    {
        currentFunction->setPersonalityFn(ehPersonalityFn);
    }

    auto* throwTy = llvm::FunctionType::get(builder->getVoidTy(),
                                            {builder->getInt32Ty(), builder->getPtrTy(), builder->getPtrTy()},
                                            false);
    auto* throwFn = module->getFunction("__djinn_throw");
    if (!throwFn)
    {
        throwFn = llvm::Function::Create(throwTy, llvm::Function::ExternalLinkage,
                                         "__djinn_throw", *module);
        throwFn->addFnAttr(llvm::Attribute::NoReturn);
    }
    builder->CreateCall(throwFn, {tag, message, typeName});
    builder->CreateUnreachable();
}
