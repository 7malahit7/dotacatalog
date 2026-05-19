#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

constexpr int PORT = 8080;
constexpr int BUFFER_SIZE = 8192;

volatile std::sig_atomic_t running = 1;

void handleSignal(int) {
    running = 0;
}

bool isProjectRoot(const fs::path& path) {
    return fs::exists(path / "prototype" / "index.html") && fs::exists(path / "static" / "css" / "style.css");
}

fs::path detectProjectRoot(const char* executablePath) {
    const fs::path currentPath = fs::current_path();
    const fs::path executableDirectory = fs::absolute(executablePath).parent_path();

    const fs::path candidates[] = {
        currentPath,
        currentPath.parent_path(),
        executableDirectory,
        executableDirectory.parent_path(),
    };

    for (const auto& candidate : candidates) {
        if (!candidate.empty() && isProjectRoot(candidate)) {
            return fs::weakly_canonical(candidate);
        }
    }

    return currentPath;
}

std::string readFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return "";
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string getMimeType(const std::string& path) {
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".html") {
        return "text/html; charset=utf-8";
    }
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".css") {
        return "text/css; charset=utf-8";
    }
    if (path.size() >= 3 && path.substr(path.size() - 3) == ".js") {
        return "application/javascript; charset=utf-8";
    }
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".png") {
        return "image/png";
    }
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".jpg") {
        return "image/jpeg";
    }
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".jpeg") {
        return "image/jpeg";
    }
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".svg") {
        return "image/svg+xml";
    }
    return "text/plain; charset=utf-8";
}

std::string stripQueryString(std::string path) {
    const auto questionMark = path.find('?');
    if (questionMark != std::string::npos) {
        path.erase(questionMark);
    }
    return path;
}

bool containsPathTraversal(const std::string& path) {
    return path.find("..") != std::string::npos;
}

std::string routeToFile(const std::string& requestPath) {
    const std::string path = stripQueryString(requestPath);

    if (path == "/" || path == "/index.html") {
        return "prototype/index.html";
    }
    if (path == "/item.html" || path.rfind("/items/", 0) == 0) {
        return "prototype/item.html";
    }
    if (path == "/admin" || path == "/admin.html") {
        return "prototype/admin.html";
    }
    if (path == "/admin/new" || path == "/admin-form.html" || path.rfind("/admin/edit/", 0) == 0) {
        return "prototype/admin-form.html";
    }
    if (path.rfind("/static/", 0) == 0 && !containsPathTraversal(path)) {
        return path.substr(1);
    }

    return "";
}

std::string makeResponse(
    int statusCode,
    const std::string& statusText,
    const std::string& body,
    const std::string& mimeType,
    bool includeBody = true
) {
    std::ostringstream response;
    response << "HTTP/1.1 " << statusCode << ' ' << statusText << "\r\n";
    response << "Content-Type: " << mimeType << "\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    if (includeBody) {
        response << body;
    }
    return response.str();
}

void sendAll(int clientSocket, const std::string& response) {
    constexpr std::size_t CHUNK_SIZE = 1024;
    std::size_t totalSent = 0;
    while (totalSent < response.size()) {
        const std::size_t bytesLeft = response.size() - totalSent;
        const std::size_t bytesToSend = std::min(CHUNK_SIZE, bytesLeft);
        const ssize_t sent = send(clientSocket, response.data() + totalSent, bytesToSend, 0);
        if (sent <= 0) {
            return;
        }
        totalSent += static_cast<std::size_t>(sent);
    }
}

void handleClient(int clientSocket, const fs::path& projectRoot) {
    int enabled = 1;
    setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));

    timeval clientTimeout = {};
    clientTimeout.tv_sec = 3;
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &clientTimeout, sizeof(clientTimeout));
    setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, &clientTimeout, sizeof(clientTimeout));

    char buffer[BUFFER_SIZE] = {};
    const ssize_t received = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

    if (received <= 0) {
        shutdown(clientSocket, SHUT_WR);
        close(clientSocket);
        return;
    }

    std::istringstream request(buffer);
    std::string method;
    std::string path;
    std::string version;
    request >> method >> path >> version;

    const bool includeBody = method != "HEAD";

    if (method != "GET" && method != "HEAD") {
        const std::string body = "<h1>405 Method Not Allowed</h1>";
        sendAll(clientSocket, makeResponse(405, "Method Not Allowed", body, "text/html; charset=utf-8", includeBody));
        shutdown(clientSocket, SHUT_WR);
        close(clientSocket);
        return;
    }

    const std::string filePath = routeToFile(path);
    if (filePath.empty()) {
        const std::string body = "<h1>404 Not Found</h1><p>Page was not found.</p>";
        sendAll(clientSocket, makeResponse(404, "Not Found", body, "text/html; charset=utf-8", includeBody));
        shutdown(clientSocket, SHUT_WR);
        close(clientSocket);
        return;
    }

    const std::string body = readFile(projectRoot / filePath);
    if (body.empty()) {
        const std::string errorBody = "<h1>404 Not Found</h1><p>File was not found.</p>";
        sendAll(clientSocket, makeResponse(404, "Not Found", errorBody, "text/html; charset=utf-8", includeBody));
        shutdown(clientSocket, SHUT_WR);
        close(clientSocket);
        return;
    }

    sendAll(clientSocket, makeResponse(200, "OK", body, getMimeType(filePath), includeBody));
    shutdown(clientSocket, SHUT_WR);
    close(clientSocket);
}

int createServerSocket() {
    const int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        std::cerr << "Failed to create socket: " << std::strerror(errno) << std::endl;
        return -1;
    }

    int enabled = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

    sockaddr_in serverAddress = {};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(PORT);

    if (bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0) {
        std::cerr << "Failed to bind port " << PORT << ": " << std::strerror(errno) << std::endl;
        close(serverSocket);
        return -1;
    }

    if (listen(serverSocket, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen on port " << PORT << ": " << std::strerror(errno) << std::endl;
        close(serverSocket);
        return -1;
    }

    return serverSocket;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    std::signal(SIGPIPE, SIG_IGN);

    const fs::path projectRoot = detectProjectRoot(argc > 0 ? argv[0] : ".");

    const int serverSocket = createServerSocket();
    if (serverSocket < 0) {
        return 1;
    }

    std::cout << "Dota Relics Catalog server is running" << std::endl;
    std::cout << "Open http://127.0.0.1:" << PORT << std::endl;
    std::cout << "Project root: " << projectRoot << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;

    while (running) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(serverSocket, &readSet);

        timeval timeout = {};
        timeout.tv_sec = 1;

        const int ready = select(serverSocket + 1, &readSet, nullptr, nullptr, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "Server select failed: " << std::strerror(errno) << std::endl;
            break;
        }
        if (ready == 0) {
            continue;
        }

        sockaddr_in clientAddress = {};
        socklen_t clientLength = sizeof(clientAddress);
        const int clientSocket = accept(serverSocket, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);

        if (clientSocket < 0) {
            if (running) {
                std::cerr << "Failed to accept client: " << std::strerror(errno) << std::endl;
            }
            continue;
        }

        handleClient(clientSocket, projectRoot);
    }

    close(serverSocket);
    std::cout << "\nServer stopped" << std::endl;
    return 0;
}
