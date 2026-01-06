//
// Created by Claude on 01/01/2026.
//

#include "Diagnostic.h"
#include <iostream>
#include <iomanip>
#include <algorithm>

void DiagnosticEngine::buildLineIndex() {
    lineOffsets_.push_back(0);
    for (size_t i = 0; i < source_.size(); ++i) {
        if (source_[i] == '\n') {
            lineOffsets_.push_back(i + 1);
        }
    }
}

std::string DiagnosticEngine::getLine(const uint32_t lineNum) const {
    if (lineNum == 0 || lineNum > lineOffsets_.size()) {
        return "";
    }

    const size_t start = lineOffsets_[lineNum - 1];
    size_t end = (lineNum < lineOffsets_.size())
                     ? lineOffsets_[lineNum] - 1
                     : source_.size();

    // Remove trailing \r if present (Windows line endings)
    if (end > start && source_[end - 1] == '\r') {
        end--;
    }

    return source_.substr(start, end - start);
}

std::string DiagnosticEngine::severityString(const Severity severity_level) {
    switch (severity_level) {
        case Severity::Error: return "error";
        case Severity::Warning: return "warning";
        case Severity::Note: return "note";
        case Severity::Help: return "help";
    }
    return "unknown";
}

std::string DiagnosticEngine::severityColor(const Severity severity_level) {
    switch (severity_level) {
        case Severity::Error: return "1;31"; // Bold red
        case Severity::Warning: return "1;33"; // Bold yellow
        case Severity::Note: return "1;36"; // Bold cyan
        case Severity::Help: return "1;32"; // Bold green
    }
    return "0";
}

constexpr std::string DiagnosticEngine::colorize(const std::string &text, const std::string &code) {
#ifdef _WIN32
    return text;
#else
    return "\033[" + code + "m" + text + "\033[0m";
#endif
}

std::string DiagnosticEngine::formatCode(const uint32_t code) {
    std::ostringstream ss;
    ss << "E" << std::setfill('0') << std::setw(4) << code;
    return ss.str();
}

void DiagnosticEngine::emit(const Diagnostic &diag) {
    diagnostics.push_back(diag);

    if (diag.severity == Severity::Error) {
        total_errors++;
    } else if (diag.severity == Severity::Warning) {
        total_warnings++;
    }
}

void DiagnosticEngine::error(const uint32_t code, const std::string &msg, const SourceLocation loc) {
    emit(Diagnostic(Severity::Error, code, msg, loc));
}

void DiagnosticEngine::warning(const uint32_t code, const std::string &msg, const SourceLocation loc) {
    emit(Diagnostic(Severity::Warning, code, msg, loc));
}

std::string DiagnosticEngine::renderDiagnostic(const Diagnostic &diag) const {
    std::ostringstream out;

    const auto severity = severityString(diag.severity);
    const auto severity_color = severityColor(diag.severity);
    const auto code_str = formatCode(diag.code);
    if (diag.location.line > 0) {
        auto lineNum = diag.location.line;
        auto col = diag.location.column;
        auto len = std::max(diag.location.length, 1u);

        auto lineNumStr = std::to_string(lineNum);
        std::string padding(lineNumStr.size(), ' ');

        out << " " << colorize("-->", "1;34") << " "
                << lineNum << ":" << col << "\n";

        out << " " << padding << " " << colorize("|", "1;34") << "\n";

        auto sourceLine = getLine(lineNum);
        out << " " << colorize(lineNumStr, "1;34") << " "
                << colorize("|", "1;34") << " " << sourceLine << "\n";

        std::string underline(col > 0 ? col - 1 : 0, ' ');
        underline += std::string(len, '^');

        out << " " << padding << " " << colorize("|", "1;34") << " "
                << colorize(underline, severity_color);

        if (!diag.label.empty()) {
            out << " " << colorize(diag.label, severity_color);
        }
        out << "\n";
    }

    // Header: error[E2001]: message
    out << colorize(severity + "[" + code_str + "]", severity_color)
            << ": " << colorize(diag.message, "1") << "\n";

    for (const auto &note: diag.notes) {
        out << " " << colorize("= note", "1;36") << ": " << note << "\n";
    }

    for (const auto &help: diag.helps) {
        out << " " << colorize("= help", "1;32") << ": " << help << "\n";
    }

    return out.str();
}

std::string DiagnosticEngine::render() const {
    std::ostringstream out;

    for (const auto &diag: diagnostics) {
        out << renderDiagnostic(diag) << "\n";
    }

    if (total_errors > 0 || total_warnings > 0) {
        out << colorize("aborting", "1;31") << " due to ";
        if (total_errors > 0) {
            out << total_errors << " error" << (total_errors > 1 ? "s" : "");
        }
        if (total_warnings > 0) {
            if (total_errors > 0) out << " and ";
            out << total_warnings << " warning" << (total_warnings > 1 ? "s" : "");
        }
        out << "\n";
    }

    return out.str();
}

const std::vector<Diagnostic> &DiagnosticEngine::get_diagnostics() const {
    return this->diagnostics;
}

void DiagnosticEngine::printToStderr(const std::basic_stacktrace<std::allocator<std::stacktrace_entry> > &stack) const {
    std::cerr << render();
    std::cerr << stack << std::endl;
}