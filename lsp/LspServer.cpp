//
// Djinn LSP Server - Implementation
//

#include "LspServer.h"
#include <filesystem>
#include <iostream>

namespace lsp {
    LspServer::LspServer() = default;

    LspServer::LspServer(std::unique_ptr<ITransport> transport)
        : _transport(std::move(transport)) {
    }

    int LspServer::run() {
        while (!_shutdown) {
            auto msg = _transport.readMessage();
            if (!msg.has_value()) {
                // EOF or error
                break;
            }
            handleMessage(msg.value());
        }
        return _shutdown ? 0 : 1;
    }

    void LspServer::handleMessage(const JsonRpcMessage &msg) {
        if (!msg.method.has_value()) {
            return;
        }

        const std::string &method = msg.method.value();

        // Requests (need response)
        if (msg.isRequest()) {
            json result;
            bool handled = true;

            if (method == "initialize") {
                result = handleInitialize(msg.params.value_or(json::object()));
            } else if (method == "shutdown") {
                result = handleShutdown();
            } else {
                handled = false;
            }

            if (handled) {
                _transport.sendResponse(msg.id.value(), result);
            } else {
                _transport.sendError(msg.id.value(), ErrorCode::MethodNotFound,
                                     "Method not found: " + method);
            }
        }
        // Notifications (no response)
        else if (msg.isNotification()) {
            if (method == "initialized") {
                handleInitialized(msg.params.value_or(json::object()));
            } else if (method == "exit") {
                handleExit();
            } else if (method == "textDocument/didOpen") {
                handleDidOpen(msg.params.value_or(json::object()));
            } else if (method == "textDocument/didChange") {
                handleDidChange(msg.params.value_or(json::object()));
            } else if (method == "textDocument/didClose") {
                handleDidClose(msg.params.value_or(json::object()));
            }
            // Unknown notifications are ignored per LSP spec
        }
    }

    json LspServer::handleInitialize(const json &params) {
        _initialized = true;

        // Extract initializationOptions from client
        if (params.contains("initializationOptions")) {
            const auto &opts = params["initializationOptions"];
            if (opts.contains("outDir")) {
                _options.outputDirectory = opts["outDir"].get<std::string>();
            }
            if (opts.contains("includeStd")) {
                _options.includeStd = opts["includeStd"].get<bool>();
            }
        }

        // Return server capabilities
        return json{
            {
                "capabilities", {
                    {
                        "textDocumentSync", {
                            {"openClose", true},
                            {"change", 1}, // Full sync (1 = Full, 2 = Incremental)
                            {"save", {{"includeText", true}}}
                        }
                    },
                    // Add more capabilities here as you implement them:
                    // {"completionProvider", {{"triggerCharacters", {".", ":"}}}},
                    // {"hoverProvider", true},
                    // {"definitionProvider", true},
                    // {"documentSymbolProvider", true},
                }
            },
            {
                "serverInfo", {
                    {"name", "djinn-lsp"},
                    {"version", "0.1.0"}
                }
            }
        };
    }

    json LspServer::handleShutdown() {
        _shutdown = true;
        return json(nullptr);
    }

    void LspServer::handleInitialized(const json & /*params*/) {
        // Server is fully initialized, can send notifications now
    }

    void LspServer::handleExit() {
        // Exit immediately
        _shutdown = true;
    }

    void LspServer::handleDidOpen(const json &params) {
        if (!params.contains("textDocument")) return;

        auto &doc = params["textDocument"];
        const std::string uri = doc["uri"].get<std::string>();
        const std::string text = doc["text"].get<std::string>();
        const int version = doc.value("version", 0);

        _documents[uri] = text;
        _documentVersions[uri] = version;

        validateDocument(uri);
    }

    void LspServer::handleDidChange(const json &params) {
        if (!params.contains("textDocument") || !params.contains("contentChanges")) return;

        const std::string uri = params["textDocument"]["uri"].get<std::string>();
        const int version = params["textDocument"].value("version", 0);

        // With full sync (change = 1), contentChanges has one element with full text
        auto &changes = params["contentChanges"];
        if (!changes.empty()) {
            _documents[uri] = changes[0]["text"].get<std::string>();
            _documentVersions[uri] = version;
        }

        validateDocument(uri);
    }

    void LspServer::handleDidClose(const json &params) {
        if (!params.contains("textDocument")) return;

        const std::string uri = params["textDocument"]["uri"].get<std::string>();
        _documents.erase(uri);
        _documentVersions.erase(uri);

        // Clear diagnostics for closed document
        publishDiagnostics(uri, {});
    }

    const std::string CLEAR_SCREEN = "\x1B[2J\x1B[1;1H";

    void LspServer::validateDocument(const std::string &uri) {
        std::cout << CLEAR_SCREEN << std::flush;
        auto it = _documents.find(uri);
        if (it == _documents.end()) return;

        const std::string &source = it->second;

        std::vector<LspDiagnostic> lspDiagnostics;

        try {
            // Compile with diagnostics only (no codegen)
            CompilerOptions options;
            options.print_ast = true;
            options.print_ir = true;
            options.silentMode = true; // Don't print to stderr in LSP mode

            // Use options from client initializationOptions
            options.outputDirectory = _options.outputDirectory;
            options.includeStd = _options.includeStd;

            // Extract filename from URI for output
            std::string filePath = uriToPath(uri);
            std::filesystem::path p(filePath);
            options.outputFileName = p.stem().string(); // e.g., "main" from "main.djinn"

            CompilerResult result = DjinnCompiler::run(source, options);

            // Convert diagnostics
            for (const auto &diag: result.diagnostics) {
                lspDiagnostics.push_back(diagnosticToLsp(diag));
            }
        } catch (const CompileError &e) {
            // Handle compile errors that escaped the compiler's catch block
            LspDiagnostic diag;
            diag.range = sourceLocationToRange(e.location());
            diag.severity = DiagnosticSeverity::Error;
            diag.code = "E" + std::to_string(static_cast<int>(e.code()));
            diag.message = e.message();
            lspDiagnostics.push_back(diag);
        } catch (const std::exception &e) {
            // Handle any other exceptions gracefully
            LspDiagnostic diag;
            diag.range = {{0, 0}, {0, 1}};
            diag.severity = DiagnosticSeverity::Error;
            diag.code = "E9999";
            diag.message = std::string("Internal error: ") + e.what();
            lspDiagnostics.push_back(diag);
        } catch (...) {
            // Handle unknown exceptions
            LspDiagnostic diag;
            diag.range = {{0, 0}, {0, 1}};
            diag.severity = DiagnosticSeverity::Error;
            diag.code = "E9999";
            diag.message = "Internal error: Unknown exception occurred";
            lspDiagnostics.push_back(diag);
        }

        publishDiagnostics(uri, lspDiagnostics);
    }

    void LspServer::publishDiagnostics(const std::string &uri, const std::vector<LspDiagnostic> &diagnostics) {
        json diagArray = json::array();
        for (const auto &diag: diagnostics) {
            diagArray.push_back({
                {
                    "range", {
                        {"start", {{"line", diag.range.start.line}, {"character", diag.range.start.character}}},
                        {"end", {{"line", diag.range.end.line}, {"character", diag.range.end.character}}}
                    }
                },
                {"severity", static_cast<int>(diag.severity)},
                {"code", diag.code},
                {"source", diag.source},
                {"message", diag.message}
            });
        }

        _transport.sendNotification("textDocument/publishDiagnostics", {
                                        {"uri", uri},
                                        {"diagnostics", diagArray}
                                    });
    }

    std::string LspServer::uriToPath(const std::string &uri) {
        // Simple file:// URI to path conversion
        std::string path = uri;
        if (path.substr(0, 8) == "file:///") {
#ifdef _WIN32
            // file:///C:/path -> C:/path
            path = path.substr(8);
#else
            // file:///path -> /path
            path = path.substr(7);
#endif
        }
        // URL decode (basic: %20 -> space, %3A -> :)
        std::string decoded;
        for (size_t i = 0; i < path.size(); ++i) {
            if (path[i] == '%' && i + 2 < path.size()) {
                int value;
                std::istringstream iss(path.substr(i + 1, 2));
                if (iss >> std::hex >> value) {
                    decoded += static_cast<char>(value);
                    i += 2;
                    continue;
                }
            }
            decoded += path[i];
        }
        return decoded;
    }

    std::string LspServer::pathToUri(const std::string &path) {
        std::string uri = "file://";
#ifdef _WIN32
        uri += "/";
#endif
        uri += path;
        return uri;
    }
} // namespace lsp