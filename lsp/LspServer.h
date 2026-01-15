//
// Djinn LSP Server - Main Server Class
//

#ifndef DJINN_LSP_SERVER_H
#define DJINN_LSP_SERVER_H

#include "JsonRpc.h"
#include "LspTypes.h"
#include "../DjinnCompiler.h"
#include <unordered_map>
#include <string>
#include <memory>

namespace lsp {
    // Options received from client via initializationOptions
    struct LspOptions {
        std::string outputDirectory = "build";
        std::string stdLibPath = "std";
        bool includeStd = false; // Default false for faster LSP validation
    };

    class LspServer {
    public:
        // Default constructor uses stdio transport
        LspServer();

        // Constructor with custom transport
        explicit LspServer(std::unique_ptr<ITransport> transport);

        ~LspServer() = default;

        // Main run loop - blocks until shutdown
        int run();

        // Get underlying transport
        ITransport *getTransport() { return _transport.getTransport(); }

    private:
        // Message handlers
        void handleMessage(const JsonRpcMessage &msg);

        // Request handlers (expect response)
        json handleInitialize(const json &params);

        json handleShutdown();

        // Notification handlers (no response)
        void handleInitialized(const json &params);

        void handleExit();

        void handleDidOpen(const json &params);

        void handleDidChange(const json &params);

        void handleDidClose(const json &params);

        // Diagnostics
        void validateDocument(const std::string &uri);

        void publishDiagnostics(const std::string &uri, const std::vector<LspDiagnostic> &diagnostics);

        // Utilities
        std::string uriToPath(const std::string &uri);

        std::string pathToUri(const std::string &path);

    private:
        JsonRpcTransport _transport;
        bool _initialized = false;
        bool _shutdown = false;
        LspOptions _options;

        // Document storage (uri -> content)
        std::unordered_map<std::string, std::string> _documents;
        std::unordered_map<std::string, int> _documentVersions;
    };
} // namespace lsp

#endif // DJINN_LSP_SERVER_H
