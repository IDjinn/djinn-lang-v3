//
// Call handler interface and implementations for function call binding
//

#ifndef DJINN_CALL_HANDLER_H
#define DJINN_CALL_HANDLER_H

#include <memory>
#include <vector>
#include "../Symbol.h"
#include "../SymbolTable.h"
#include "../../parser/ast/Expression.h"
#include "../../diagnostics/Diagnostic.h"

class Binder; // Forward declaration

namespace djinn::binder {
    // Uow reference parameters (struct params of uow members, lowered to
    // pointers) accept a mutable variable or field chain — the member mutates
    // the caller's object through them.
    inline const std::string* uow_reference_argument_root(const Expression &expr)
    {
        if (const auto *ident = dynamic_cast<const Identifier *>(&expr))
            return &ident->identifier.token_name;
        if (const auto *field = dynamic_cast<const FieldAccess *>(&expr))
            return uow_reference_argument_root(*field->object);
        return nullptr;
    }

    template <typename SymT>
    inline void check_uow_reference_argument(
        const SymT &sym, const Expression &arg, const size_t paramIdx,
        const std::shared_ptr<ScopedSymbolTable> &scope, DiagnosticEngine &diagnostics)
    {
        const std::string *root = uow_reference_argument_root(arg);
        if (!root)
        {
            diagnostics.emitAndPrint(Diagnostic(
                Severity::Error, DiagnosticCode::TYPE_MISMATCH,
                "parameter '" + sym.paramNames[paramIdx] + "' of uow member '" + sym.name +
                    "' is passed by reference and needs a mutable struct variable",
                arg.location));
            return;
        }
        const auto symbol = scope->lookupVariable(*root);
        if (!symbol || !symbol->isMutable)
        {
            diagnostics.emitAndPrint(Diagnostic(
                Severity::Error, DiagnosticCode::TYPE_MISMATCH,
                "cannot pass '" + *root + "' by reference to uow member '" + sym.name +
                    "': the argument must be declared 'mut'",
                arg.location));
        }
    }

    // Shared body of the by-ref check once the callee's paramTypes are known:
    // validates the bound argument against the parameter at paramIdx.
    template <typename SymT>
    inline void check_uow_reference_param(
        const SymT &sym, const size_t paramIdx, const Expression &arg,
        const std::shared_ptr<Symbol> &bound,
        const std::shared_ptr<ScopedSymbolTable> &scope, DiagnosticEngine &diagnostics)
    {
        const Type &paramType = sym.paramTypes[paramIdx];
        if (!(paramType.kind == TypeKind::POINTER && paramType.elementType &&
              paramType.elementType->kind == TypeKind::STRUCT))
            return;
        if (bound && bound->type.kind == TypeKind::STRUCT)
            check_uow_reference_argument(sym, arg, paramIdx, scope, diagnostics);
        else if (!bound || bound->type.kind != TypeKind::POINTER)
        {
            diagnostics.emitAndPrint(Diagnostic(
                Severity::Error, DiagnosticCode::TYPE_MISMATCH,
                "parameter '" + sym.paramNames[paramIdx] + "' of uow member '" + sym.name +
                    "' is passed by reference and needs a mutable struct argument",
                arg.location));
        }
    }

    // Base class for all call handlers
    class CallHandler {
    public:
        virtual ~CallHandler() = default;

        // Returns true if this handler can process the call
        virtual bool canHandle(const FunctionCall &call,
                               std::shared_ptr<ScopedSymbolTable> scope) const = 0;

        // Processes the call and returns the symbol
        virtual std::shared_ptr<Symbol> handle(
            const FunctionCall &call,
            Binder &binder,
            std::shared_ptr<ScopedSymbolTable> scope,
            DiagnosticEngine &diagnostics
        ) = 0;
    };

    // Handler for method calls (receiver.method())
    class MethodCallHandler : public CallHandler {
    public:
        bool canHandle(const FunctionCall &call,
                       std::shared_ptr<ScopedSymbolTable> scope) const override;

        std::shared_ptr<Symbol> handle(
            const FunctionCall &call,
            Binder &binder,
            std::shared_ptr<ScopedSymbolTable> scope,
            DiagnosticEngine &diagnostics
        ) override;
    };

    // Handler for intrinsics (sizeof, alignof, etc.)
    class IntrinsicCallHandler : public CallHandler {
    public:
        bool canHandle(const FunctionCall &call,
                       std::shared_ptr<ScopedSymbolTable> scope) const override;

        std::shared_ptr<Symbol> handle(
            const FunctionCall &call,
            Binder &binder,
            std::shared_ptr<ScopedSymbolTable> scope,
            DiagnosticEngine &diagnostics
        ) override;
    };

    // Handler for enum construction (Enum::Variant())
    class EnumConstructionHandler : public CallHandler {
    public:
        bool canHandle(const FunctionCall &call,
                       std::shared_ptr<ScopedSymbolTable> scope) const override;

        std::shared_ptr<Symbol> handle(
            const FunctionCall &call,
            Binder &binder,
            std::shared_ptr<ScopedSymbolTable> scope,
            DiagnosticEngine &diagnostics
        ) override;
    };

    // Handler for constructor calls (StructName(args))
    class ConstructorCallHandler : public CallHandler {
    public:
        bool canHandle(const FunctionCall &call,
                       std::shared_ptr<ScopedSymbolTable> scope) const override;

        std::shared_ptr<Symbol> handle(
            const FunctionCall &call,
            Binder &binder,
            std::shared_ptr<ScopedSymbolTable> scope,
            DiagnosticEngine &diagnostics
        ) override;
    };

    // Handler for regular function calls
    class RegularFunctionCallHandler : public CallHandler {
    public:
        bool canHandle(const FunctionCall &call,
                       std::shared_ptr<ScopedSymbolTable> scope) const override;

        std::shared_ptr<Symbol> handle(
            const FunctionCall &call,
            Binder &binder,
            std::shared_ptr<ScopedSymbolTable> scope,
            DiagnosticEngine &diagnostics
        ) override;
    };

    // Factory function to create the handler chain
    std::vector<std::unique_ptr<CallHandler> > createCallHandlers();
} // namespace djinn::binder

#endif // DJINN_CALL_HANDLER_H