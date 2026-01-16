//
// Djinn LSP Server - JSON-RPC Communication
//

#ifndef DJINN_LSP_JSONRPC_H
#define DJINN_LSP_JSONRPC_H

#include <string>
#include <optional>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>

// Forward declare socket types to avoid Windows macro conflicts
#ifdef _WIN32
using SocketHandle = unsigned long long; // SOCKET is UINT_PTR
#else
using SocketHandle = int;
#endif

namespace lsp {
    using json = nlohmann::json;

    // JSON-RPC Message types
    struct JsonRpcMessage {
        std::string jsonrpc = "2.0";
        std::optional<json> id;
        std::optional<std::string> method;
        std::optional<json> params;
        std::optional<json> result;
        std::optional<json> error;

        bool isRequest() const { return method.has_value() && id.has_value(); }
        bool isNotification() const { return method.has_value() && !id.has_value(); }
        bool isResponse() const { return !method.has_value() && id.has_value(); }
    };

    // JSON-RPC Error Codes
    enum class ErrorCode {
        ParseError = -32700,
        InvalidRequest = -32600,
        MethodNotFound = -32601,
        InvalidParams = -32602,
        InternalError = -32603,
        ServerNotInitialized = -32002,
        RequestCancelled = -32800,
    };

    // Transport mode
    enum class TransportMode {
        Stdio,
        Socket
    };

    // Abstract transport interface
    class ITransport {
    public:
        virtual ~ITransport() = default;

        virtual std::string readHeader() = 0;

        virtual std::string readContent(size_t length) = 0;

        virtual void writeRaw(const std::string &data) = 0;

        virtual bool isConnected() const = 0;
    };

    // Stdio transport (stdin/stdout)
    class StdioTransport : public ITransport {
    public:
        StdioTransport();

        std::string readHeader() override;

        std::string readContent(size_t length) override;

        void writeRaw(const std::string &data) override;

        bool isConnected() const override { return true; }
    };

    // Socket transport (TCP)
    class SocketTransport : public ITransport {
    public:
        explicit SocketTransport(int port);

        ~SocketTransport() override;

        bool listen();

        bool acceptConnection();

        void closeConnection();

        std::string readHeader() override;

        std::string readContent(size_t length) override;

        void writeRaw(const std::string &data) override;

        bool isConnected() const override;

        int getPort() const { return _port; }

    private:
        static constexpr SocketHandle INVALID_SOCKET_VALUE = static_cast<SocketHandle>(-1);

        int _port;
        SocketHandle _serverSocket = INVALID_SOCKET_VALUE;
        SocketHandle _clientSocket = INVALID_SOCKET_VALUE;
        bool _wsaInitialized = false;
    };

    // Transport layer for LSP
    class JsonRpcTransport {
    public:
        // Default constructor uses stdio
        JsonRpcTransport();

        // Constructor with custom transport
        explicit JsonRpcTransport(std::unique_ptr<ITransport> transport);

        ~JsonRpcTransport() = default;

        // Read a message (blocking)
        std::optional<JsonRpcMessage> readMessage();

        // Write a message
        void writeMessage(const JsonRpcMessage &msg);

        // Send a response
        void sendResponse(const json &id, const json &result);

        // Send an error response
        void sendError(const json &id, ErrorCode code, const std::string &message);

        // Send a notification (no id, no response expected)
        void sendNotification(const std::string &method, const json &params);

        // Get underlying transport
        ITransport *getTransport() { return _transport.get(); }

    private:
        std::unique_ptr<ITransport> _transport;
    };
} // namespace lsp

#endif // DJINN_LSP_JSONRPC_H