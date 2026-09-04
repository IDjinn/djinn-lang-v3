//
// Debug info emission (line tables only): one compile unit per module, one
// subprogram per generated function and per-statement debug locations. This
// is what makes native backtraces symbolize to file:line without any runtime
// bookkeeping — the binary's DWARF/CodeView records carry the mapping.
//

#ifndef DJINN_DEBUG_INFO_H
#define DJINN_DEBUG_INFO_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

#include "../diagnostics/Diagnostic.h"

namespace djinn
{
    class SourceDebugInfo
    {
    public:
        SourceDebugInfo(llvm::Module& module, llvm::IRBuilder<>& builder);

        void set_enabled(bool on) { enabled_ = on; }
        [[nodiscard]] bool enabled() const { return enabled_; }

        // Creates the compile unit; no-op when disabled.
        void begin_module(const std::string& name);

        // Attaches a subprogram to the function being generated and points the
        // builder at its definition line. Calls nest: end_function restores
        // the location that was current before the call, so on-demand
        // generation (monomorphization, properties) stays consistent.
        void begin_function(llvm::Function* fn, const std::string& displayName, const SourceLocation& loc);

        void end_function();

        // Updates the builder's current debug location; no-op when disabled.
        void set_current_location(const SourceLocation& loc);

        // Completes the metadata graph; must run before module verification
        // or printing. No-op (and permanent) once already finalized.
        void finalize();

    private:
        llvm::DIFile* get_or_create_file(const std::string& fileId);

        llvm::Module& module_;
        llvm::IRBuilder<>& builder_;
        std::unique_ptr<llvm::DIBuilder> di_;
        llvm::DICompileUnit* compileUnit_ = nullptr;
        llvm::DISubprogram* currentSubprogram_ = nullptr;
        llvm::DISubroutineType* subroutineType_ = nullptr;
        std::unordered_map<std::string, llvm::DIFile*> files_;

        struct SavedScope
        {
            llvm::DISubprogram* subprogram;
            llvm::DebugLoc location;
        };

        std::vector<SavedScope> scopeStack_;

        bool enabled_ = false;
        bool finalized_ = false;
    };
}

#endif // DJINN_DEBUG_INFO_H
