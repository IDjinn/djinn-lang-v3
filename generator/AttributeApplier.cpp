//
// Centralized attribute application — maps Djinn attributes to LLVM IR attributes
//

#include "Generator.h"
#include "../utils/Logger.h"

void Generator::apply_attributes(llvm::Function* func, const std::vector<AttributeSymbol>& attrs)
{
    for (const auto& attr : attrs)
    {
        if (attr.name == "ForceInline")
        {
            func->addFnAttr(llvm::Attribute::AlwaysInline);
        }
        else if (attr.name == "NoInline")
        {
            func->addFnAttr(llvm::Attribute::NoInline);
        }
        else if (attr.name == "NoReturn")
        {
            func->addFnAttr(llvm::Attribute::NoReturn);
        }
        else if (attr.name == "Cold")
        {
            func->addFnAttr(llvm::Attribute::Cold);
        }
        else if (attr.name == "Hot")
        {
            func->addFnAttr(llvm::Attribute::Hot);
        }
        else if (attr.name == "NoSync")
        {
            func->addFnAttr(llvm::Attribute::NoSync);
        }
        else if (attr.name == "NoUnwind")
        {
            func->addFnAttr(llvm::Attribute::NoUnwind);
        }
        else if (attr.name == "WillReturn")
        {
            func->addFnAttr(llvm::Attribute::WillReturn);
        }
        else if (attr.name == "NoRecurse")
        {
            func->addFnAttr(llvm::Attribute::NoRecurse);
        }
        else if (attr.name == "Llvm")
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
    // Default mode has no unwinding — every function is nounwind. Native
    // exceptions keep nounwind off generated functions (any of them may end
    // up with an invoke/landing pad); declarations of externs keep it.
    if (nativeExceptions && !func->isDeclaration())
    {
        return;
    }
    if (!func->hasFnAttribute(llvm::Attribute::NoUnwind))
    {
        func->addFnAttr(llvm::Attribute::NoUnwind);
    }
}
