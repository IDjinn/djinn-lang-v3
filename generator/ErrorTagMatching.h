//
// Catch-style hierarchy matching for error tags: an arm of error type T
// matches an error whose tag is T's or any derived type's tag. Shared by the
// outcome-switch lowering and the native try/catch lowering.
//

#ifndef DJINN_ERROR_TAG_MATCHING_H
#define DJINN_ERROR_TAG_MATCHING_H

#include <cstdint>
#include <vector>

#include "../binder/SymbolTable.h"

namespace djinn
{
    inline bool error_derived_from(const ScopedSymbolTable& symbols, const StructSymbol& derived,
                                   const StructSymbol& base)
    {
        std::string current = derived.errorBase;
        while (!current.empty())
        {
            if (current == base.name) return true;
            const auto sym = symbols.lookupStruct(current);
            if (!sym) return false;
            current = sym->errorBase;
        }
        return false;
    }

    // Tags an arm of error type `armType` matches: its own tag plus the tags of
    // every error type deriving from it
    inline std::vector<int32_t> error_arm_matched_tags(const ScopedSymbolTable& symbols,
                                                       const StructSymbol& armType)
    {
        std::vector<int32_t> tags{armType.errorTag};
        for (const auto& entry : symbols.symbols())
        {
            const auto& symbol = entry.second;
            if (!symbol || !symbol->isStruct()) continue;
            const auto structSym = std::dynamic_pointer_cast<StructSymbol>(symbol);
            if (!structSym || !structSym->isErrorType || structSym->errorTag == armType.errorTag) continue;
            if (error_derived_from(symbols, *structSym, armType))
                tags.push_back(structSym->errorTag);
        }
        return tags;
    }
}

#endif // DJINN_ERROR_TAG_MATCHING_H
