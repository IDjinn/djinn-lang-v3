//
// In-process execution of generated modules via ORC LLJIT.
// Replaces the per-test "clang + system(exe)" round-trip used by tests.
//

#ifndef DJINN_JITRUNNER_H
#define DJINN_JITRUNNER_H

#include <memory>
#include <string>
#include <utility>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

namespace djinn
{
    // Loads (and lazily compiles via clang -emit-llvm, cached on disk by content
    // hash) the C runtime as bitcode. Returns false if the runtime bitcode
    // cannot be produced — callers should fall back to the clang executable path.
    bool jitRuntimeAvailable();

    // Runs `main` from the module under LLJIT with the runtime bitcode linked in.
    // Takes ownership of the module and its context. Returns the program exit
    // code; negative on internal JIT failure (caller should treat as an error).
    // When the program traps, outRuntimeErrorReport (if given) receives the
    // rendered runtime error report (source snippet, caret, values, stack trace).
    // inMemorySource, when given, is the djinn source text of the module (file
    // id "main"); it is registered with the runtime so error reports can render
    // source snippets — the caller keeps it alive until the call returns.
    int executeModule(std::unique_ptr<llvm::Module> module, std::unique_ptr<llvm::LLVMContext> context,
                      int optimizationLevel, std::string* outRuntimeErrorReport = nullptr,
                      const std::string* inMemorySource = nullptr);
}

#endif //DJINN_JITRUNNER_H
