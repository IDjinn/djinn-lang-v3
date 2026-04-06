//
// Centralized attribute application — maps Djinn attributes to LLVM IR attributes
//

#include "Generator.h"
#include "../utils/Logger.h"

void Generator::apply_attributes(llvm::Function* func, const std::vector<AttributeSymbol>& attrs)
{
    for (const auto& attr : attrs)
    {
        if (attr.name == "force-inline")
        {
            func->addFnAttr(llvm::Attribute::AlwaysInline);
        }
        else if (attr.name == "no-inline")
        {
            func->addFnAttr(llvm::Attribute::NoInline);
        }
        else if (attr.name == "noreturn")
        {
            func->addFnAttr(llvm::Attribute::NoReturn);
        }
        else if (attr.name == "cold")
        {
            func->addFnAttr(llvm::Attribute::Cold);
        }
        else if (attr.name == "hot")
        {
            func->addFnAttr(llvm::Attribute::Hot);
        }
        else if (attr.name == "nosync")
        {
            func->addFnAttr(llvm::Attribute::NoSync);
        }
        else if (attr.name == "nounwind")
        {
            func->addFnAttr(llvm::Attribute::NoUnwind);
        }
        else if (attr.name == "willreturn")
        {
            func->addFnAttr(llvm::Attribute::WillReturn);
        }
        else if (attr.name == "norecurse")
        {
            func->addFnAttr(llvm::Attribute::NoRecurse);
        }
        else if (attr.name == "llvm")
        {
            // Escape hatch: pass raw LLVM attribute strings
            for (const auto& arg : attr.args)
            {
                if (auto* str = std::get_if<std::string>(&arg.value))
                {
                    func->addFnAttr(*str);
                }
            }
        }
    }
}

void Generator::apply_implicit_attributes(llvm::Function* func)
{
    // Djinn has no exceptions — all functions are nounwind
    if (!func->hasFnAttribute(llvm::Attribute::NoUnwind))
    {
        func->addFnAttr(llvm::Attribute::NoUnwind);
    }
}
