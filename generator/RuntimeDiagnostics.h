//
// Shadow-stack pass: inserts __djinn_frame_pop() before every return
// instruction of functions that push a frame at entry, so all return paths
// (explicit returns, error propagation, cleanup) unwind the trace.
//

#ifndef DJINN_SHADOW_STACK_PASS_H
#define DJINN_SHADOW_STACK_PASS_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/ADT/SmallVector.h"

namespace djinn
{
    struct ShadowStackPopPass : llvm::PassInfoMixin<ShadowStackPopPass>
    {
        llvm::PreservedAnalyses run(llvm::Function& fn, llvm::FunctionAnalysisManager&)
        {
            using namespace llvm;
            if (fn.isDeclaration()) return PreservedAnalyses::all();

            bool hasPush = false;
            for (const auto& bb : fn)
            {
                for (const auto& inst : bb)
                {
                    if (const auto* cb = dyn_cast<CallBase>(&inst))
                    {
                        const auto* callee = cb->getCalledFunction();
                        if (callee && callee->getName() == "__djinn_frame_push")
                        {
                            hasPush = true;
                            break;
                        }
                    }
                }
                if (hasPush) break;
            }
            // Async/coroutine functions and hand-written wrappers never push,
            // so they are left untouched (their frames are simply absent from
            // the trace).
            if (!hasPush) return PreservedAnalyses::all();

            auto* module = fn.getParent();
            auto* popFn = module->getFunction("__djinn_frame_pop");
            if (!popFn)
            {
                popFn = llvm::Function::Create(
                    llvm::FunctionType::get(llvm::Type::getVoidTy(fn.getContext()), false),
                    llvm::Function::ExternalLinkage, "__djinn_frame_pop", *module);
            }

            SmallVector<ReturnInst*, 8> rets;
            for (auto& bb : fn)
                for (auto& inst : bb)
                    if (auto* ret = dyn_cast<ReturnInst>(&inst))
                        rets.push_back(ret);

            for (auto* ret : rets)
            {
                IRBuilder<> builder(ret);
                builder.CreateCall(popFn);
            }

            return rets.empty() ? PreservedAnalyses::all() : PreservedAnalyses::none();
        }
    };
} // namespace djinn

#endif // DJINN_SHADOW_STACK_PASS_H
