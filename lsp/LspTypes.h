//
// Djinn LSP Server - Basic Types
//

#ifndef DJINN_LSP_TYPES_H
#define DJINN_LSP_TYPES_H

#include <string>
#include <vector>
#include <optional>
#include "../diagnostics/Diagnostic.h"

namespace lsp {
    // LSP Position (0-based line and character)
    struct Position {
        int line = 0;
        int character = 0;
    };

    // LSP Range
    struct Range {
        Position start;
        Position end;
    };

    // LSP Location
    struct Location {
        std::string uri;
        Range range;
    };

    // LSP Diagnostic Severity
    enum class DiagnosticSeverity {
        Error = 1,
        Warning = 2,
        Information = 3,
        Hint = 4
    };

    // LSP Diagnostic
    struct LspDiagnostic {
        Range range;
        DiagnosticSeverity severity;
        std::string code;
        std::string source = "djinn";
        std::string message;
    };

    // Convert Djinn SourceLocation to LSP Range
    inline Range sourceLocationToRange(const SourceLocation &loc) {
        Range r;
        // LSP is 0-based, Djinn is 1-based for both lines and columns
        r.start.line = loc.line > 0 ? static_cast<int>(loc.line - 1) : 0;
        r.start.character = loc.column > 0 ? static_cast<int>(loc.column - 1) : 0;
        r.end.line = r.start.line;
        r.end.character = r.start.character + static_cast<int>(loc.length);
        return r;
    }

    // Convert Djinn Severity to LSP DiagnosticSeverity
    inline DiagnosticSeverity severityToLsp(Severity sev) {
        switch (sev) {
            case Severity::Error: return DiagnosticSeverity::Error;
            case Severity::Warning: return DiagnosticSeverity::Warning;
            case Severity::Note: return DiagnosticSeverity::Information;
            case Severity::Help: return DiagnosticSeverity::Hint;
        }
        return DiagnosticSeverity::Error;
    }

    // Convert Djinn Diagnostic to LSP Diagnostic
    inline LspDiagnostic diagnosticToLsp(const Diagnostic &diag) {
        LspDiagnostic lspDiag;
        lspDiag.range = sourceLocationToRange(diag.location);
        lspDiag.severity = severityToLsp(diag.severity);
        lspDiag.code = "E" + std::to_string(diag.code);
        lspDiag.message = diag.message;
        return lspDiag;
    }

    // Text Document Item
    struct TextDocumentItem {
        std::string uri;
        std::string languageId;
        int version;
        std::string text;
    };

    // Text Document Identifier
    struct TextDocumentIdentifier {
        std::string uri;
    };

    // Versioned Text Document Identifier
    struct VersionedTextDocumentIdentifier {
        std::string uri;
        int version;
    };

    // Text Document Content Change Event
    struct TextDocumentContentChangeEvent {
        std::optional<Range> range;
        std::string text;
    };
} // namespace lsp

#endif // DJINN_LSP_TYPES_H
