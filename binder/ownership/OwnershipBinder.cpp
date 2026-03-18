//
// Ownership checking integration with Binder
//

#include "../Binder.h"

using namespace djinn::ownership;

void Binder::trackVariableDefinition(const std::string& name, const Type& type, SourceLocation loc)
{
    // Resolve transparent types for copy semantics
    Type resolvedType = type;
    if (type.kind == TypeKind::STRUCT && !type.isTransparent)
    {
        if (const auto structSym = _global_scope->lookupStruct(type.structName))
        {
            if (structSym->isTransparent() && structSym->baseType)
            {
                resolvedType.isTransparent = true;
            }
        }
    }
    _ownership.defineVariable(name, resolvedType, loc);
}

void Binder::checkVariableUse(const std::string& name, SourceLocation loc)
{
    if (const auto result = _ownership.checkUse(name, loc); !result.success)
    {
        if (result.relatedLocation.has_value())
        {
            const auto message = result.errorMessage +
                " (moved at line " + std::to_string(result.relatedLocation->line) + ")";
            BINDER_ERROR(DiagnosticCode::USE_AFTER_MOVE, message, nullptr, loc);
        }
        else
        {
            BINDER_ERROR(DiagnosticCode::USE_AFTER_MOVE, result.errorMessage, nullptr, loc);
        }
    }
}

void Binder::checkVariableMove(const std::string& name, SourceLocation loc)
{
    auto result = _ownership.checkMove(name, loc);
    if (!result.success)
    {
        if (result.relatedLocation.has_value())
        {
            BINDER_ERROR(DiagnosticCode::MOVE_ERROR, result.errorMessage +
                         " (at line " + std::to_string(result.relatedLocation->line) + ")",
                         nullptr, loc);
        }
        else
        {
            BINDER_ERROR(DiagnosticCode::MOVE_ERROR, result.errorMessage, nullptr, loc);
        }
    }
}

void Binder::checkVariableBorrow(const std::string& name, BorrowKind kind,
                                 const std::string& borrower, SourceLocation loc)
{
    auto result = _ownership.checkBorrow(name, kind, borrower, loc);
    if (!result.success)
    {
        uint32_t code = (kind == BorrowKind::Mutable)
                            ? DiagnosticCode::MUTABLE_BORROW_CONFLICT
                            : DiagnosticCode::BORROW_CONFLICT;

        if (result.relatedLocation.has_value())
        {
            BINDER_ERROR(code, result.errorMessage +
                         " (borrowed at line " + std::to_string(result.relatedLocation->line) + ")",
                         nullptr, loc);
        }
        else
        {
            BINDER_ERROR(code, result.errorMessage, nullptr, loc);
        }
    }
}

void Binder::checkVariableAssignment(const std::string& name, SourceLocation loc)
{
    auto result = _ownership.checkAssign(name, loc);
    if (!result.success)
    {
        if (result.relatedLocation.has_value())
        {
            BINDER_ERROR(DiagnosticCode::ASSIGN_TO_BORROWED, result.errorMessage +
                         " (borrowed at line " + std::to_string(
                             result.relatedLocation->line) + ")",
                         nullptr, loc);
        }
        else
        {
            BINDER_ERROR(DiagnosticCode::ASSIGN_TO_BORROWED, result.errorMessage, nullptr, loc);
        }
    }
}

void Binder::performMove(const std::string& name, SourceLocation loc)
{
    _ownership.doMove(name, loc);
}

void Binder::performBorrow(const std::string& name, BorrowKind kind,
                           const std::string& borrower, SourceLocation loc)
{
    _ownership.doBorrow(name, kind, borrower, loc);
}

void Binder::reinitializeVariable(const std::string& name)
{
    _ownership.reinitialize(name);
}