//
// Created by Claude on 01/01/2026.
//

#ifndef DJINN_DIAGNOSTIC_H
#define DJINN_DIAGNOSTIC_H

#include <complex.h>
#include <string>
#include <vector>
#include <sstream>
#include <cstdint>
#include <stacktrace>

enum class Severity { Error, Warning, Note, Help };

// Diagnostic codes follow the pattern: XYYY
// X = Category (1=Lexer, 2=Parser, 3=Semantic, 4=CodeGen)
// YYY = Specific error number
namespace DiagnosticCode {
    // Lexer errors (1xxx)
    constexpr uint32_t UNEXPECTED_CHARACTER = 1001;
    constexpr uint32_t UNTERMINATED_STRING = 1002;
    constexpr uint32_t INVALID_NUMBER = 1003;

    // Parser errors (2xxx)
    constexpr uint32_t UNEXPECTED_TOKEN = 2001;
    constexpr uint32_t EXPECTED_EXPRESSION = 2002;
    constexpr uint32_t EXPECTED_TYPE = 2003;
    constexpr uint32_t EXPECTED_IDENTIFIER = 2004;
    constexpr uint32_t EXPECTED_SEMICOLON = 2005;
    constexpr uint32_t EXPECTED_CLOSING_BRACE = 2006;
    constexpr uint32_t EXPECTED_CLOSING_PAREN = 2007;
    constexpr uint32_t EXPECTED_OPENING_BRACE = 2008;
    constexpr uint32_t EXPECTED_OPENING_PAREN = 2009;
    constexpr uint32_t EXPECTED_EQUAL = 2010;

    // Semantic errors (3xxx)
    constexpr uint32_t UNDEFINED_VARIABLE = 3001;
    constexpr uint32_t UNDEFINED_FUNCTION = 3002;
    constexpr uint32_t UNDEFINED_STRUCT = 3003;
    constexpr uint32_t UNDEFINED_FIELD = 3004;
    constexpr uint32_t TYPE_MISMATCH = 3005;
    constexpr uint32_t NOT_A_STRUCT = 3006;
    constexpr uint32_t DUPLICATE_DEFINITION = 3007;
    constexpr uint32_t MIXED_INITIALIZERS = 3008;

    // CodeGen errors (4xxx)
    constexpr uint32_t INVALID_TYPE = 4001;
    constexpr uint32_t INVALID_OPERATION = 4002;
    constexpr uint32_t UNSUPPORTED_OPERATOR = 4003;
    constexpr uint32_t IMMUTABLE_MODIFICATION = 4004;
    constexpr uint32_t INVALID_ARGUMENT_COUNT = 4005;
}

struct SourceLocation {
    uint32_t line = 0;
    uint32_t column = 0;
    uint32_t length = 1;

    SourceLocation() = default;

    SourceLocation(const uint32_t l, const uint32_t c, const uint32_t len = 1)
        : line(l), column(c), length(len) {
    }
};

struct Diagnostic {
    Severity severity;
    uint32_t code;
    std::string message;
    SourceLocation location;
    std::string label;
    std::vector<std::string> notes;
    std::vector<std::string> helps;

    Diagnostic(const Severity sev, const uint32_t code, std::string msg, const SourceLocation loc = {})
        : severity(sev), code(code), message(std::move(msg)), location(loc) {
    }

    Diagnostic &withLabel(const std::string &lbl) {
        label = lbl;
        return *this;
    }

    Diagnostic &withNote(const std::string &note) {
        notes.push_back(note);
        return *this;
    }

    Diagnostic &withHelp(const std::string &help) {
        helps.push_back(help);
        return *this;
    }
};

class DiagnosticEngine {
public:
    explicit DiagnosticEngine(std::string source) : source_(std::move(source)) {
        buildLineIndex();
    }

    void emit(const Diagnostic &diag);

    void error(uint32_t code, const std::string &msg, SourceLocation loc = {});

    void warning(uint32_t code, const std::string &msg, SourceLocation loc = {});

    [[nodiscard]] bool hasErrors() const { return total_errors > 0; }
    [[nodiscard]] size_t errorCount() const { return total_errors; }
    [[nodiscard]] size_t warningCount() const { return total_warnings; }

    [[nodiscard]] std::string render() const;

    [[nodiscard]] const std::vector<Diagnostic> &get_diagnostics() const;

    void printToStderr(const std::basic_stacktrace<std::allocator<std::stacktrace_entry> > &stack) const;

private:
    std::string source_;
    std::vector<size_t> lineOffsets_;
    std::vector<Diagnostic> diagnostics;
    size_t total_errors = 0;
    size_t total_warnings = 0;

    void buildLineIndex();

    [[nodiscard]] std::string getLine(uint32_t lineNum) const;

    [[nodiscard]] std::string renderDiagnostic(const Diagnostic &diag) const;

    [[nodiscard]] static std::string severityString(Severity severity_level);

    [[nodiscard]] static std::string severityColor(Severity severity_level);

    [[nodiscard]] static constexpr std::string colorize(const std::string &text, const std::string &code);

    [[nodiscard]] static std::string formatCode(uint32_t code);
};

class CompileError : public std::exception {
public:
    CompileError(const uint32_t code, std::string msg, const SourceLocation loc = {})
        : code_(code), message_(std::move(msg)), location_(loc) {
    }

    [[nodiscard]] uint32_t code() const { return code_; }
    [[nodiscard]] const std::string &message() const { return message_; }
    [[nodiscard]] const SourceLocation &location() const { return location_; }
    [[nodiscard]] const char *what() const noexcept override { return message_.c_str(); }

private:
    uint32_t code_;
    std::string message_;
    SourceLocation location_;
};

#endif //DJINN_DIAGNOSTIC_H