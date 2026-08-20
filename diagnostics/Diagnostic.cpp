//
// Created by Claude on 01/01/2026.
//

#include "Diagnostic.h"
#include <iostream>
#include <iomanip>
#include <algorithm>

#include "../utils/Logger.h"

std::vector<size_t> DiagnosticEngine::buildLineIndex(const std::string& source)
{
    std::vector<size_t> offsets;
    offsets.push_back(0);
    for (size_t i = 0; i < source.size(); ++i)
    {
        if (source[i] == '\n')
        {
            offsets.push_back(i + 1);
        }
    }
    return offsets;
}

void DiagnosticEngine::registerSource(const std::string& fileId, std::string source)
{
    SourceFile file;
    file.lineOffsets = buildLineIndex(source);
    file.source = std::move(source);
    _sources[fileId] = std::move(file);
    LOG_DEBUG("registering file %s", fileId.c_str());
}

std::string DiagnosticEngine::getLine(const std::string& fileId, const uint32_t lineNum) const
{
    auto it = _sources.find(fileId);
    if (it == _sources.end())
    {
        it = _sources.find("main.djinn");
    }

    if (it == _sources.end()) return "";

    const auto& [source, lineOffsets] = it->second;
    if (lineNum == 0 || lineNum > lineOffsets.size())
    {
        return "";
    }

    const size_t start = lineOffsets[lineNum - 1];
    size_t end = (lineNum < lineOffsets.size())
                     ? lineOffsets[lineNum] - 1
                     : source.size();

    if (end > start && source[end - 1] == '\r')
    {
        end--;
    }

    return source.substr(start, end - start);
}

std::string DiagnosticEngine::severityString(const Severity severity_level)
{
    switch (severity_level)
    {
    case Severity::Error: return "error";
    case Severity::Warning: return "warning";
    case Severity::Note: return "note";
    case Severity::Help: return "help";
    }
    return "unknown";
}

std::string DiagnosticEngine::severityColor(const Severity severity_level)
{
    switch (severity_level)
    {
    case Severity::Error: return "1;31"; // Bold red
    case Severity::Warning: return "1;33"; // Bold yellow
    case Severity::Note: return "1;36"; // Bold cyan
    case Severity::Help: return "1;32"; // Bold green
    }
    return "0";
}

std::string DiagnosticEngine::colorize(const std::string& text, const std::string& code)
{
#ifdef _WIN32
    return text;
#else
    return "\033[" + code + "m" + text + "\033[0m";
#endif
}

std::string DiagnosticEngine::formatCode(const uint32_t code)
{
    std::ostringstream ss;
    ss << "E" << std::setfill('0') << std::setw(4) << code;
    return ss.str();
}

void DiagnosticEngine::emit(const Diagnostic& diag)
{
    // Skip exact duplicates: some paths emit a diagnostic and then re-throw it
    // as a CompileError that gets emitted again by a top-level catch handler
    for (const auto& existing : diagnostics)
    {
        if (existing.severity == diag.severity &&
            existing.code == diag.code &&
            existing.message == diag.message &&
            existing.location.fileId == diag.location.fileId &&
            existing.location.line == diag.location.line &&
            existing.location.column == diag.location.column)
        {
            return;
        }
    }

    diagnostics.push_back(diag);

    if (diag.severity == Severity::Error)
    {
        total_errors++;
    }
    else if (diag.severity == Severity::Warning)
    {
        total_warnings++;
    }
}

void DiagnosticEngine::error(const uint32_t code, const std::string& msg, const SourceLocation loc)
{
    emit(Diagnostic(Severity::Error, code, msg, loc));
}

void DiagnosticEngine::warning(const uint32_t code, const std::string& msg, const SourceLocation loc)
{
    emit(Diagnostic(Severity::Warning, code, msg, loc));
}

void DiagnosticEngine::emitAndPrint(const Diagnostic& diag)
{
    emit(diag);
}

std::string DiagnosticEngine::renderDiagnostic(const Diagnostic& diag) const
{
    std::ostringstream out;

    const auto severity = severityString(diag.severity);
    const auto severity_color = severityColor(diag.severity);
    const auto code_str = formatCode(diag.code);
    if (diag.location.line > 0)
    {
        auto lineNum = diag.location.line;
        auto col = diag.location.column;
        auto len = std::max(diag.location.length, 1u);

        auto lineNumStr = std::to_string(lineNum);
        std::string padding(lineNumStr.size(), ' ');
        {
            // --> main.djinn:3:11
            std::string fileInfo = diag.location.fileId.empty() ? "" : diag.location.fileId + ":";
            out << " " << colorize("-->", "1;34") << " "
                << fileInfo << lineNum << ":" << col << "\n";
        }
        {
            out << " " << padding << " " << colorize("|", "1;34") << "\n";

            auto sourceLine = getLine(diag.location.fileId, lineNum);
            out << " " << colorize(lineNumStr, "1;34") << " "
                << colorize("|", "1;34") << " " << sourceLine << "\n";

            std::string underline(col > 0 ? col - 1 : 0, ' ');
            underline += std::string(len, '^'); //  ^^^^^^

            out << " " << padding << " " << colorize("|", "1;34") << " "
                << colorize(underline, severity_color);
        }

        if (!diag.label.empty())
        {
            out << " " << colorize(diag.label, severity_color);
        }
        out << "\n";
    }

    // error[E2001]: message
    out << colorize(severity + "[" + code_str + "]", severity_color)
        << ": " << colorize(diag.message, "1") << "\n";

    out << colorize(std::format("line: {}\ncolumn: {}\n", diag.location.line, diag.location.column), severity_color);

    for (const auto& note : diag.notes)
    {
        out << " " << colorize("= note", "1;36") << ": " << note << "\n";
    }

    for (const auto& help : diag.helps)
    {
        out << " " << colorize("= help", "1;32") << ": " << help << "\n";
    }

    return out.str();
}

std::string DiagnosticEngine::render() const
{
    std::ostringstream out;

    for (const auto& diag : diagnostics)
    {
        out << renderDiagnostic(diag) << "\n";
    }

    if (total_errors > 0)
    {
        out << colorize("aborting", "1;31") << " due to "
            << total_errors << " error" << (total_errors > 1 ? "s" : "");
        if (total_warnings > 0)
        {
            out << " and " << total_warnings << " warning" << (total_warnings > 1 ? "s" : "");
        }
        out << "\n";
    }
    else if (total_warnings > 0)
    {
        out << colorize(std::to_string(total_warnings) + " warning"
                        + (total_warnings > 1 ? "s" : "") + " generated.", "1;33") << "\n";
    }

    return out.str();
}

const std::vector<Diagnostic>& DiagnosticEngine::get_diagnostics() const
{
    return this->diagnostics;
}

void DiagnosticEngine::printToStderr(const std::basic_stacktrace<std::allocator<std::stacktrace_entry>>& stack) const
{
    std::cerr << render() << std::endl;
    std::cerr << stack << std::endl;
}