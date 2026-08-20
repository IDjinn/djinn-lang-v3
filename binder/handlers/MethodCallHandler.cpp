//
// MethodCallHandler implementation
//

#include "CallHandler.h"
#include "../Binder.h"

namespace djinn::binder
{
    bool MethodCallHandler::canHandle(const FunctionCall& call,
                                      std::shared_ptr<ScopedSymbolTable> /*scope*/) const
    {
        return call.isMethodCall();
    }

    std::shared_ptr<Symbol> MethodCallHandler::handle(
        const FunctionCall& call,
        Binder& binder,
        std::shared_ptr<ScopedSymbolTable> /*scope*/,
        DiagnosticEngine& /*diagnostics*/)
    {
        const auto receiver = binder.bindExpression(*call.receiver);
        if (!receiver) return nullptr;

        std::vector<std::shared_ptr<Symbol>> parameters;
        for (const auto& arg : call.arguments)
        {
            parameters.emplace_back(binder.bindExpression(*arg));
        }

        // Resolve method return type from the struct
        Type returnType = Type::voided();
        std::string structName;
        if (receiver->type.kind == TypeKind::POINTER && receiver->type.elementType)
        {
            structName = receiver->type.elementType->structName;
        }
        else if (receiver->type.kind == TypeKind::STRUCT)
        {
            structName = receiver->type.structName;
        }

        std::shared_ptr<Symbol> methodSym = nullptr;
        if (!structName.empty())
        {
            if (const auto structSym = binder._global_scope->lookupStruct(structName))
            {
                if (const auto method = structSym->getMethod(call.name.token_name))
                {
                    returnType = method->returnType;
                    methodSym = method;
                }
            }
        }

        // If method not found on struct (e.g., generic T), search interfaces
        if (!methodSym)
        {
            for (const auto& sym : binder._global_scope->get_all_interfaces())
            {
                const auto ifaceSym = std::dynamic_pointer_cast<InterfaceSymbol>(sym);
                if (!ifaceSym) continue;
                if (const auto method = ifaceSym->getMethod(call.name.token_name))
                {
                    returnType = method->returnType;
                    methodSym = method;
                    break;
                }
            }
        }

        // Calls to throwing methods require `try` (or a throwing caller to propagate)
        if (const auto method = std::dynamic_pointer_cast<MethodSymbol>(methodSym))
        {
            binder.check_throwing_call(call.name.token_name, method->isThrowing(), call.name.location);
        }

        auto result = std::make_shared<FunctionCallSymbol>(
            call.name.token_name, receiver, methodSym,
            std::move(parameters), call.name.location
        );
        result->type = returnType;
        return result;
    }
} // namespace djinn::binder