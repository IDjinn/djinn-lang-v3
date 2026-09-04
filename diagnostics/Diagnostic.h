//
// Created by Claude on 01/01/2026.
//

#ifndef DJINN_DIAGNOSTIC_H
#define DJINN_DIAGNOSTIC_H

#include <assert.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <cstdint>
#include <iostream>
#include <stacktrace>

#include "../lexer/Token.h"
#include "../utils/Logger.h"

enum class Severity { Error, Warning, Note, Help };

// Diagnostic codes follow the pattern: XYYY
// X = Category (1=Lexer, 2=Parser, 3=Semantic, 4=CodeGen)
// YYY = Specific error number
namespace DiagnosticCode
{
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
    constexpr uint32_t EXPECTED_CATCH_OR_FINALLY = 2011;

    // Semantic errors (3xxx)
    constexpr uint32_t UNDEFINED_VARIABLE = 3001;
    constexpr uint32_t UNDEFINED_FUNCTION = 3002;
    constexpr uint32_t UNDEFINED_STRUCT = 3003;
    constexpr uint32_t UNDEFINED_FIELD = 3004;
    constexpr uint32_t TYPE_MISMATCH = 3005;
    constexpr uint32_t NOT_A_STRUCT = 3006;
    constexpr uint32_t DUPLICATE_DEFINITION = 3007;
    constexpr uint32_t MIXED_INITIALIZERS = 3008;
    constexpr uint32_t INVALID_OPERAND = 3009;
    constexpr uint32_t UNDEFINED_INTERFACE = 3010;
    constexpr uint32_t MISSING_INTERFACE_METHOD_IMPLEMENTATION = 3011;
    constexpr uint32_t GENERIC_CONSTRAINT_VIOLATION = 3012;
    constexpr uint32_t IMPL_METHOD_SIGNATURE_MISMATCH = 3013;
    constexpr uint32_t UNDEFINED_INTRINSIC_FUNCTION = 3014;
    constexpr uint32_t OPERATOR_NOT_IMPLEMENTED = 3015;

    // CodeGen errors (4xxx)
    constexpr uint32_t INVALID_TYPE = 4001;
    constexpr uint32_t INVALID_OPERATION = 4002;
    constexpr uint32_t UNSUPPORTED_OPERATOR = 4003;
    constexpr uint32_t IMMUTABLE_MODIFICATION = 4004;
    constexpr uint32_t INVALID_ARGUMENT_COUNT = 4005;
    constexpr uint32_t INVALID_MODIFIERS = 4006;

    // Ownership errors (5xxx)
    constexpr uint32_t USE_AFTER_MOVE = 5001;
    constexpr uint32_t MOVE_ERROR = 5002;
    constexpr uint32_t BORROW_CONFLICT = 5003;
    constexpr uint32_t MUTABLE_BORROW_CONFLICT = 5004;
    constexpr uint32_t ASSIGN_TO_BORROWED = 5005;
    constexpr uint32_t BORROW_OUTLIVES_VALUE = 5006;
    constexpr uint32_t DOUBLE_MOVE = 5007;

    // Compile-time if (7xxx)
    constexpr uint32_t CONSTEXPR_RUNTIME_IF = 7001;
    constexpr uint32_t NOT_COMPILE_TIME_EVALUABLE = 7002;

    // Macro warnings (6xxx)
    constexpr uint32_t MACRO_POSSIBLE_SIDE_EFFECT = 6001;

    // Attribute errors (8xxx)
    constexpr uint32_t INVALID_ATTRIBUTE_TARGET = 8001;

    // Error handling (9xxx)
    constexpr uint32_t MISSING_TRY = 9001;
    constexpr uint32_t THROW_OUTSIDE_THROWS = 9002;
    constexpr uint32_t THROWS_TYPE_MISMATCH = 9003;
    constexpr uint32_t TRY_ON_NON_THROWING = 9004;
    constexpr uint32_t TRY_WITHOUT_FALLBACK = 9005;
    constexpr uint32_t CONSTEXPR_CALL_THROWS = 9006;
    constexpr uint32_t CONTRACT_VIOLATION_COMPILE_TIME = 9007;
    constexpr uint32_t SWITCH_ARM_UNREACHABLE = 9008;
    constexpr uint32_t TRY_CATCH_REQUIRES_EXCEPTIONS = 9009;
    constexpr uint32_t CATCH_ARM_NOT_ERROR_TYPE = 9010;
}

struct SourceLocation
{
    std::string fileId;
    uint32_t line = 0;
    uint32_t column = 0;
    uint32_t length = 1;

    SourceLocation() = default;

    SourceLocation(const uint32_t l, const uint32_t c, const uint32_t len = 1)
        : line(l), column(c), length(len)
    {
    }

    SourceLocation(std::string file, const uint32_t l, const uint32_t c, const uint32_t len = 1)
        : fileId(std::move(file)), line(l), column(c), length(len)
    {
    }

    explicit SourceLocation(Position position, const uint32_t length)
        : fileId(std::move(position.fileId)), line(position.line), column(position.column), length(length)
    {
    }

    SourceLocation operator-(const SourceLocation& start) const
    {
        if (fileId != start.fileId && fileId.length() > 0 && start.fileId.length() > 0)
        {
            std::cerr
                << "Tried to compare different source file location!\n"
                << "\t" << fileId << "\n"
                << "\t" << start.fileId
                << std::endl;
            assert(false);
        }

        SourceLocation result;

        result.fileId = fileId.empty() ? start.fileId : fileId;
        result.line = start.line;
        result.column = start.column;

        const uint32_t endColumn = column + length;
        result.length = endColumn - start.column;

        return result;
    }
};

struct Diagnostic
{
    Severity severity;
    uint32_t code;
    std::string message;
    SourceLocation location;
    std::string label;
    std::vector<std::string> notes;
    std::vector<std::string> helps;

    Diagnostic(const Severity sev, const uint32_t code, std::string msg, const SourceLocation loc = {})
        : severity(sev), code(code), message(std::move(msg)), location(loc)
    {
    }

    Diagnostic& withLabel(const std::string& lbl)
    {
        label = lbl;
        return *this;
    }

    Diagnostic& withNote(const std::string& note)
    {
        notes.push_back(note);
        return *this;
    }

    Diagnostic& withHelp(const std::string& help)
    {
        helps.push_back(help);
        return *this;
    }
};

struct SourceFile
{
    std::string source;
    std::vector<size_t> lineOffsets;
};

class DiagnosticEngine
{
public:
    DiagnosticEngine() = default;

    explicit DiagnosticEngine(std::string source)
    {
        registerSource("main", std::move(source));
    }

    void registerSource(const std::string& fileId, std::string source);

    void emit(const Diagnostic& diag);

    void error(uint32_t code, const std::string& msg, SourceLocation loc = {});

    void warning(uint32_t code, const std::string& msg, SourceLocation loc = {});

    void emitAndPrint(const Diagnostic& diag);

    [[nodiscard]] bool hasErrors() const { return total_errors > 0; }
    [[nodiscard]] size_t errorCount() const { return total_errors; }
    [[nodiscard]] size_t warningCount() const { return total_warnings; }

    [[nodiscard]] std::string render() const;

    [[nodiscard]] const std::vector<Diagnostic>& get_diagnostics() const;

    [[nodiscard]] const std::unordered_map<std::string, SourceFile>& getSources() const { return _sources; }

    [[nodiscard]] std::string getLine(const std::string& fileId, uint32_t lineNum) const;

    void printToStderr(const std::basic_stacktrace<std::allocator<std::stacktrace_entry>>& stack) const;

private:
    std::unordered_map<std::string, SourceFile> _sources;
    std::vector<Diagnostic> diagnostics;
    size_t total_errors = 0;
    size_t total_warnings = 0;

    static std::vector<size_t> buildLineIndex(const std::string& source);

    [[nodiscard]] std::string renderDiagnostic(const Diagnostic& diag) const;

    [[nodiscard]] static std::string severityString(Severity severity_level);

    [[nodiscard]] static std::string severityColor(Severity severity_level);

    [[nodiscard]] static std::string colorize(const std::string& text, const std::string& code);

    [[nodiscard]] static std::string formatCode(uint32_t code);
};

class CompileError : public std::exception
{
public:
    CompileError(const uint32_t code, std::string msg, const SourceLocation loc = {})
        : code_(code), message_(std::move(msg)), location_(loc)
    {
    }

    [[nodiscard]] uint32_t code() const { return code_; }
    [[nodiscard]] const std::string& message() const { return message_; }
    [[nodiscard]] const SourceLocation& location() const { return location_; }
    [[nodiscard]] const char* what() const noexcept override { return message_.c_str(); }

private:
    uint32_t code_;
    std::string message_;
    SourceLocation location_;
};

#endif //DJINN_DIAGNOSTIC_H