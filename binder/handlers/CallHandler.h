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