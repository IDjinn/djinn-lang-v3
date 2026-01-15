//
// Djinn LSP Server - Entry Point
//

#include "LspServer.h"
#include <iostream>
#include <string>

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  --stdio           Use stdio transport (default)\n"
              << "  --socket <port>   Listen on TCP port\n"
              << "  --port <port>     Alias for --socket\n"
              << "  --help            Show this help message\n"
              << "\n"
              << "Examples:\n"
              << "  " << programName << "                   # Start with stdio\n"
              << "  " << programName << " --socket 5007     # Listen on port 5007\n";
}

int runSocketServer(int port) {
    std::cerr << "Djinn LSP Server starting on port " << port << "...\n";

    while (true) {
        auto socketTransport = std::make_unique<lsp::SocketTransport>(port);

        if (!socketTransport->listen()) {
            std::cerr << "Error: Failed to listen on port " << port << "\n";
            return 1;
        }

        std::cerr << "Waiting for connection on port " << port << "...\n";

        if (!socketTransport->acceptConnection()) {
            std::cerr << "Error: Failed to accept connection\n";
            continue;
        }

        std::cerr << "Client connected.\n";

        lsp::LspServer server(std::move(socketTransport));
        int result = server.run();

        std::cerr << "Client disconnected. Result: " << result << "\n";

        // Continue loop to accept new connections
    }
}

int main(int argc, char* argv[]) {
    bool useSocket = false;
    int port = 5007;  // Default port

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "--stdio") {
            useSocket = false;
        }
        else if (arg == "--socket" || arg == "--port") {
            useSocket = true;
            if (i + 1 < argc) {
                try {
                    port = std::stoi(argv[++i]);
                } catch (const std::exception&) {
                    std::cerr << "Error: Invalid port number\n";
                    return 1;
                }
            }
        }
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (useSocket) {
        return runSocketServer(port);
    }
    else {
        // Stdio mode (default)
        lsp::LspServer server;
        return server.run();
    }
}
