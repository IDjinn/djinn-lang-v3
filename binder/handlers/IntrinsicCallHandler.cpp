//
// IntrinsicCallHandler implementation
//

#include "CallHandler.h"
#include "../Binder.h"
#include "../../generator/Intrinsics.h"

namespace djinn::binder
{
    bool IntrinsicCallHandler::canHandle(const FunctionCall& call,
                                         std::shared_ptr<ScopedSymbolTable> /*scope*/) const
    {
        return is_intrinsic(call.name.token_name);
    }

    std::shared_ptr<Symbol> IntrinsicCallHandler::handle(
        const FunctionCall& call,
        Binder& binder,
        std::shared_ptr<ScopedSymbolTable> scope,
        DiagnosticEngine& /*diagnostics*/)
    {
        std::vector<std::shared_ptr<Symbol>> parameters;

        // sizeof, alignof and typeof take type arguments (which may be generic params like T),
        // so we skip binding their arguments to avoid "undefined variable" errors
        const auto intrinsic = get_intrinsic(call.name.token_name);
        const bool isTypeIntrinsic = intrinsic && (*intrinsic == Intrinsic::Sizeof
            || *intrinsic == Intrinsic::Alignof || *intrinsic == Intrinsic::Typeof);

        if (!isTypeIntrinsic)
        {
            for (const auto& arg : call.arguments)
            {
                parameters.emplace_back(binder.bindExpression(*arg));
            }
        }

        // Resolve typeof at bind-time: store the human-readable type name in the AST node
        if (intrinsic && *intrinsic == Intrinsic::Typeof && !call.arguments.empty())
        {
            const auto& arg = *call.arguments[0];
            if (const auto* ident = dynamic_cast<const Identifier*>(&arg))
            {
                if (auto sym = scope->lookupVariable(ident->identifier.token_name))
                {
                    // For auto variables, try to infer from the initializer
                    if (sym->type.kind == TypeKind::AUTO)
                    {
                        if (auto inferred = binder.inferExpressionType(arg))
                        {
                            call.typeofResolvedName = inferred->toHumanString();
                        }
                    }
                    else
                    {
                        call.typeofResolvedName = sym->type.toHumanString();
                    }
                }
            }
            // Fallback: try inferring the expression type directly (literals, binary, etc.)
            if (call.typeofResolvedName.empty())
            {
                if (auto inferred = binder.inferExpressionType(arg))
                {
                    call.typeofResolvedName = inferred->toHumanString();
                }
            }
        }

        // Determine return type for intrinsics
        Type returnType = Type::voided();
        if (intrinsic)
        {
            switch (*intrinsic)
            {
            case Intrinsic::Sizeof:
            case Intrinsic::Alignof:
                returnType = Type::integer(32, false);
                break;
            default:
                break;
            }
        }

        auto intrinsicSym = std::make_shared<FunctionSymbol>(
            call.name.token_name, returnType, call.name.location);
        auto result = std::make_shared<FunctionCallSymbol>(
            call.name.token_name, intrinsicSym, std::move(parameters), call.name.location);
        result->type = returnType;
        return result;
    }
} // namespace djinn::binder