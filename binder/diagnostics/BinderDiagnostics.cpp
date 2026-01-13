//
// Created by Claude on 05/01/2026.
//

#include "../Binder.h"

void Binder::errorImmutableVariable(const std::string &name, const SourceLocation loc) const {
    _diagnostics.error(DiagnosticCode::IMMUTABLE_MODIFICATION,
                       "tried to modify a immutable variable '" + name + "'", loc);
}

void Binder::errorUndefinedVariable(const std::string &name, const SourceLocation loc) const {
    _diagnostics.error(DiagnosticCode::UNDEFINED_VARIABLE,
                       "undefined variable '" + name + "'", loc);
}

void Binder::errorUndefinedFunction(const std::string &name, const SourceLocation loc) const {
    _diagnostics.error(DiagnosticCode::UNDEFINED_FUNCTION,
                       "undefined function '" + name + "'", loc);
}

void Binder::errorUndefinedStruct(const std::string &name, const SourceLocation loc) const {
    _diagnostics.error(DiagnosticCode::UNDEFINED_STRUCT,
                       "undefined struct '" + name + "'", loc);
}

void Binder::errorUndefinedField(const std::string &structName, const std::string &fieldName,
                                 const SourceLocation loc) const {
    _diagnostics.error(DiagnosticCode::UNDEFINED_FIELD,
                       "struct '" + structName + "' has no field named '" + fieldName + "'", loc);
}

void Binder::errorDuplicateDefinition(const std::string &name, const SymbolKind kind, const SourceLocation loc) const {
    _diagnostics.error(DiagnosticCode::DUPLICATE_DEFINITION,
                       Symbol::kindToString(kind) + " '" + name + "' is already defined", loc);
}

void Binder::errorWrongArgumentCount(const std::string &funcName, const size_t expected, const size_t got,
                                     const SourceLocation loc) const {
    _diagnostics.error(DiagnosticCode::TYPE_MISMATCH,
                       "function '" + funcName + "' expects " + std::to_string(expected) +
                       " arguments but got " + std::to_string(got), loc);
}