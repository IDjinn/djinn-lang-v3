//
// IntrinsicCallHandler implementation
//

#include "CallHandler.h"
#include "../Binder.h"
#include "../../generator/Intrinsics.h"

namespace djinn::binder {
    bool IntrinsicCallHandler::canHandle(const FunctionCall &call,
                                         std::shared_ptr<ScopedSymbolTable> /*scope*/) const {
        return is_intrinsic(call.name.token_name);
    }

    std::shared_ptr<Symbol> IntrinsicCallHandler::handle(
        const FunctionCall &call,
        Binder &binder,
        std::shared_ptr<ScopedSymbolTable> scope,
        DiagnosticEngine & /*diagnostics*/) {
        std::vector<std::shared_ptr<Symbol> > parameters;

        // sizeof and alignof take type arguments (which may be generic params like T),
        // so we skip binding their arguments to avoid "undefined variable" errors
        const auto intrinsic = get_intrinsic(call.name.token_name);
        const bool isTypeIntrinsic = intrinsic && (*intrinsic == Intrinsic::Sizeof || *intrinsic == Intrinsic::Alignof);

        if (!isTypeIntrinsic) {
            for (const auto &arg: call.arguments) {
                parameters.emplace_back(binder.bindExpression(*arg));
            }
        }

        auto intrinsicSym = std::make_shared<FunctionSymbol>(
            call.name.token_name, Type::voided(), call.name.location);
        return std::make_shared<FunctionCallSymbol>(
            call.name.token_name, intrinsicSym, std::move(parameters), call.name.location);
    }
} // namespace djinn::binder