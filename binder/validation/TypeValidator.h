//
// Type validation utilities extracted from Binder
//

#ifndef DJINN_TYPE_VALIDATOR_H
#define DJINN_TYPE_VALIDATOR_H

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "../SymbolTable.h"
#include "../../diagnostics/Diagnostic.h"
#include "../../parser/ast/Type.h"
#include "../../parser/ast/Expression.h"
#include "../../parser/ast/Declaration.h"

namespace djinn::binder {
    class TypeValidator {
    public:
        TypeValidator(DiagnosticEngine &diagnostics,
                      std::shared_ptr<ScopedSymbolTable> globalScope);

        // Set the current scope for lookups
        void setCurrentScope(std::shared_ptr<ScopedSymbolTable> scope);

        // Validation of type existence
        bool isTypeDefined(Type &type) const;

        bool isTypeDefined(const Type &type) const;

        // Type compatibility checking
        void checkTypeCompatibility(const Type &expected,
                                    const Expression &expr,
                                    SourceLocation loc);

        // Type inference
        std::optional<Type> inferExpressionType(const Expression &expr) const;

        // Type resolution
        std::unique_ptr<Type> resolveType(const Type &type) const;

        // Check if a type is a generic parameter
        static bool isGenericType(const Type &type, const StructDeclaration &struc);

    private:
        DiagnosticEngine &_diagnostics;
        std::shared_ptr<ScopedSymbolTable> _globalScope;
        std::shared_ptr<ScopedSymbolTable> _currentScope;
    };
} // namespace djinn::binder

#endif // DJINN_TYPE_VALIDATOR_H