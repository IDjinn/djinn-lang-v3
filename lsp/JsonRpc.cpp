//
// Djinn LSP Server - JSON-RPC Implementation
//

// Include socket headers FIRST to avoid macro conflicts
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
// Undefine Windows macros that conflict with our code
#undef VOID
#undef INT
#undef FLOAT
#undef CONST
#undef ERROR
#endif

#include "JsonRpc.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cstring>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace lsp {
    // ============================================================================
    // StdioTransport Implementation
    // ============================================================================

    StdioTransport::StdioTransport() {
#ifdef _WIN32
        // Set stdin/stdout to binary mode on Windows
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        // Disable buffering for stdout
        std::cout.setf(std::ios::unitbuf);
    }

    std::string StdioTransport::readHeader() {
        std::string header;
        char c;
        while (std::cin.get(c)) {
            header += c;
            // Headers end with \r\n\r\n
            if (header.size() >= 4 &&
                header.substr(header.size() - 4) == "\r\n\r\n") {
                return header;
            }
        }
        return "";
    }

    std::string StdioTransport::readContent(size_t length) {
        std::string content(length, '\0');
        std::cin.read(&content[0], static_cast<std::streamsize>(length));
        return content;
    }

    void StdioTransport::writeRaw(const std::string &data) {
        std::cout << "Content-Length: " << data.size() << "\r\n\r\n" << data;
        std::cout.flush();
    }

    // ============================================================================
    // SocketTransport Implementation
    // ============================================================================

    SocketTransport::SocketTransport(int port) : _port(port) {
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
            _wsaInitialized = true;
        }
#endif
    }

    bool SocketTransport::isConnected() const {
        return _clientSocket != INVALID_SOCKET_VALUE;
    }

    SocketTransport::~SocketTransport() {
        closeConnection();
        if (_serverSocket != INVALID_SOCKET_VALUE) {
#ifdef _WIN32
            closesocket(static_cast<SOCKET>(_serverSocket));
#else
            close(_serverSocket);
#endif
        }
#ifdef _WIN32
        if (_wsaInitialized) {
            WSACleanup();
        }
#endif
    }

    bool SocketTransport::listen() {
#ifdef _WIN32
        if (!_wsaInitialized) return false;
        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) {
            return false;
        }
        _serverSocket = static_cast<SocketHandle>(sock);
#else
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1) {
            return false;
        }
        _serverSocket = sock;
#endif

        // Allow address reuse
        int opt = 1;
#ifdef _WIN32
        setsockopt(static_cast<SOCKET>(_serverSocket), SOL_SOCKET, SO_REUSEADDR, (const char *) &opt, sizeof(opt));
#else
        setsockopt(_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(static_cast<uint16_t>(_port));

#ifdef _WIN32
        if (bind(static_cast<SOCKET>(_serverSocket), reinterpret_cast<sockaddr *>(&serverAddr),
                 sizeof(serverAddr)) < 0) {
            return false;
        }
        if (::listen(static_cast<SOCKET>(_serverSocket), 1) < 0) {
            return false;
        }
#else
        if (bind(_serverSocket, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)) < 0) {
            return false;
        }
        if (::listen(_serverSocket, 1) < 0) {
            return false;
        }
#endif

        return true;
    }

    bool SocketTransport::acceptConnection() {
        sockaddr_in clientAddr{};
#ifdef _WIN32
        int clientLen = sizeof(clientAddr);
        SOCKET client = accept(static_cast<SOCKET>(_serverSocket), reinterpret_cast<sockaddr *>(&clientAddr),
                               &clientLen);
        if (client == INVALID_SOCKET) {
            return false;
        }
        _clientSocket = static_cast<SocketHandle>(client);
#else
        socklen_t clientLen = sizeof(clientAddr);
        int client = accept(_serverSocket, reinterpret_cast<sockaddr *>(&clientAddr), &clientLen);
        if (client == -1) {
            return false;
        }
        _clientSocket = client;
#endif
        return true;
    }

    void SocketTransport::closeConnection() {
        if (_clientSocket != INVALID_SOCKET_VALUE) {
#ifdef _WIN32
            closesocket(static_cast<SOCKET>(_clientSocket));
#else
            close(_clientSocket);
#endif
            _clientSocket = INVALID_SOCKET_VALUE;
        }
    }

    std::string SocketTransport::readHeader() {
        if (_clientSocket == INVALID_SOCKET_VALUE) return "";

        std::string header;
        char c;
        while (true) {
#ifdef _WIN32
            int received = recv(static_cast<SOCKET>(_clientSocket), &c, 1, 0);
#else
            int received = recv(_clientSocket, &c, 1, 0);
#endif
            if (received <= 0) {
                return "";
            }
            header += c;
            // Headers end with \r\n\r\n
            if (header.size() >= 4 &&
                header.substr(header.size() - 4) == "\r\n\r\n") {
                return header;
            }
        }
    }

    std::string SocketTransport::readContent(size_t length) {
        if (_clientSocket == INVALID_SOCKET_VALUE) return "";

        std::string content(length, '\0');
        size_t totalReceived = 0;

        while (totalReceived < length) {
#ifdef _WIN32
            int received = recv(static_cast<SOCKET>(_clientSocket), &content[totalReceived],
                                static_cast<int>(length - totalReceived), 0);
#else
            int received = recv(_clientSocket, &content[totalReceived],
                                static_cast<int>(length - totalReceived), 0);
#endif
            if (received <= 0) {
                return "";
            }
            totalReceived += received;
        }

        return content;
    }

    void SocketTransport::writeRaw(const std::string &data) {
        if (_clientSocket == INVALID_SOCKET_VALUE) return;

        std::string message = "Content-Length: " + std::to_string(data.size()) + "\r\n\r\n" + data;

        size_t totalSent = 0;
        while (totalSent < message.size()) {
#ifdef _WIN32
            int sent = send(static_cast<SOCKET>(_clientSocket), message.c_str() + totalSent,
                            static_cast<int>(message.size() - totalSent), 0);
#else
            int sent = send(_clientSocket, message.c_str() + totalSent,
                            static_cast<int>(message.size() - totalSent), 0);
#endif
            if (sent <= 0) {
                return;
            }
            totalSent += sent;
        }
    }

    // ============================================================================
    // JsonRpcTransport Implementation
    // ============================================================================

    JsonRpcTransport::JsonRpcTransport()
        : _transport(std::make_unique<StdioTransport>()) {
    }

    JsonRpcTransport::JsonRpcTransport(std::unique_ptr<ITransport> transport)
        : _transport(std::move(transport)) {
    }

    std::optional<JsonRpcMessage> JsonRpcTransport::readMessage() {
        std::string header = _transport->readHeader();
        if (header.empty()) {
            return std::nullopt;
        }

        // Parse Content-Length header
        size_t contentLength = 0;
        std::istringstream headerStream(header);
        std::string line;
        while (std::getline(headerStream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) continue;

            auto colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                std::string key = line.substr(0, colonPos);
                std::string value = line.substr(colonPos + 1);
                // Trim whitespace
                while (!value.empty() && value.front() == ' ') {
                    value.erase(0, 1);
                }
                if (key == "Content-Length") {
                    contentLength = std::stoull(value);
                }
            }
        }

        if (contentLength == 0) {
            return std::nullopt;
        }

        // Read content
        std::string content = _transport->readContent(contentLength);

        try {
            json j = json::parse(content);
            JsonRpcMessage msg;
            msg.jsonrpc = j.value("jsonrpc", "2.0");

            if (j.contains("id")) {
                msg.id = j["id"];
            }
            if (j.contains("method")) {
                msg.method = j["method"].get<std::string>();
            }
            if (j.contains("params")) {
                msg.params = j["params"];
            }
            if (j.contains("result")) {
                msg.result = j["result"];
            }
            if (j.contains("error")) {
                msg.error = j["error"];
            }

            return msg;
        } catch (const json::exception &) {
            return std::nullopt;
        }
    }

    void JsonRpcTransport::writeMessage(const JsonRpcMessage &msg) {
        json j;
        j["jsonrpc"] = msg.jsonrpc;

        if (msg.id.has_value()) {
            j["id"] = msg.id.value();
        }
        if (msg.method.has_value()) {
            j["method"] = msg.method.value();
        }
        if (msg.params.has_value()) {
            j["params"] = msg.params.value();
        }
        if (msg.result.has_value()) {
            j["result"] = msg.result.value();
        }
        if (msg.error.has_value()) {
            j["error"] = msg.error.value();
        }

        _transport->writeRaw(j.dump());
    }

    void JsonRpcTransport::sendResponse(const json &id, const json &result) {
        JsonRpcMessage msg;
        msg.id = id;
        msg.result = result;
        writeMessage(msg);
    }

    void JsonRpcTransport::sendError(const json &id, ErrorCode code, const std::string &message) {
        JsonRpcMessage msg;
        msg.id = id;
        msg.error = json{
            {"code", static_cast<int>(code)},
            {"message", message}
        };
        writeMessage(msg);
    }

    void JsonRpcTransport::sendNotification(const std::string &method, const json &params) {
        JsonRpcMessage msg;
        msg.method = method;
        msg.params = params;
        writeMessage(msg);
    }
} // namespace lsp
