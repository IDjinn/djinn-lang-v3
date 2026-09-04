#include "DebugInfo.h"

#include "llvm/IR/DebugInfoMetadata.h"

namespace djinn
{
    SourceDebugInfo::SourceDebugInfo(llvm::Module& module, llvm::IRBuilder<>& builder)
        : module_(module), builder_(builder)
    {
    }

    void SourceDebugInfo::begin_module(const std::string& name)
    {
        if (!enabled_ || di_) return;

        di_ = std::make_unique<llvm::DIBuilder>(module_);
        const std::string fileName = name.empty() ? "djinn_module" : name;
        auto* file = di_->createFile(fileName, ".");
        compileUnit_ = di_->createCompileUnit(
            llvm::dwarf::DW_LANG_C99,
            file,
            "djinn",
            false,
            "",
            0,
            "",
            llvm::DICompileUnit::DebugEmissionKind::LineTablesOnly
        );
    }

    llvm::DIFile* SourceDebugInfo::get_or_create_file(const std::string& fileId)
    {
        const std::string key = fileId.empty() ? "djinn" : fileId;
        if (const auto it = files_.find(key); it != files_.end())
            return it->second;

        std::string directory = ".";
        std::string name = key;
        const auto sep = key.find_last_of("/\\");
        if (sep != std::string::npos)
        {
            name = key.substr(sep + 1);
            directory = key.substr(0, sep);
            if (directory.empty()) directory = ".";
        }
        auto* file = di_->createFile(name, directory);
        files_.emplace(key, file);
        return file;
    }

    void SourceDebugInfo::begin_function(llvm::Function* fn, const std::string& displayName,
                                         const SourceLocation& loc)
    {
        if (!di_ || !compileUnit_) return;

        if (!subroutineType_)
        {
            subroutineType_ = di_->createSubroutineType(di_->getOrCreateTypeArray({}));
        }

        auto* file = get_or_create_file(loc.fileId);
        const unsigned line = loc.line ? loc.line : 1;
        // On-demand generation can re-enter a function that is already being
        // generated; reusing its subprogram keeps every in-function debug
        // location resolving to the subprogram attached to the Function.
        auto* subprogram = fn->getSubprogram();
        if (!subprogram)
        {
            subprogram = di_->createFunction(
                compileUnit_,
                displayName,
                fn->getName(),
                file,
                line,
                subroutineType_,
                line,
                llvm::DINode::FlagZero,
                llvm::DISubprogram::SPFlagDefinition
            );
            fn->setSubprogram(subprogram);
        }

        scopeStack_.push_back({currentSubprogram_, builder_.getCurrentDebugLocation()});
        currentSubprogram_ = subprogram;
        builder_.SetCurrentDebugLocation(llvm::DebugLoc(
            llvm::DILocation::get(module_.getContext(), line, loc.column ? loc.column : 1, subprogram)));
    }

    void SourceDebugInfo::end_function()
    {
        if (scopeStack_.empty()) return;

        const auto& saved = scopeStack_.back();
        currentSubprogram_ = saved.subprogram;
        builder_.SetCurrentDebugLocation(saved.location);
        scopeStack_.pop_back();
    }

    void SourceDebugInfo::set_current_location(const SourceLocation& loc)
    {
        if (!di_ || !currentSubprogram_ || !loc.line) return;

        builder_.SetCurrentDebugLocation(llvm::DebugLoc(
            llvm::DILocation::get(module_.getContext(), loc.line,
                                  loc.column ? loc.column : 1, currentSubprogram_)));
    }

    void SourceDebugInfo::finalize()
    {
        if (!di_ || finalized_) return;
        finalized_ = true;
        di_->finalize();
    }
}
