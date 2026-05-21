#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "database.h"
#include "views.h"

namespace {

namespace fs = std::filesystem;

constexpr int PORT = 8080;
constexpr int BUFFER_SIZE = 8192;
constexpr std::size_t MAX_REQUEST_SIZE = 1024 * 1024;

volatile std::sig_atomic_t running = 1;

struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int statusCode = 200;
    std::string statusText = "OK";
    std::string body;
    std::string mimeType = "text/html; charset=utf-8";
    std::vector<std::pair<std::string, std::string>> headers;
};

void handleSignal(int) {
    running = 0;
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool isProjectRoot(const fs::path& path) {
    return fs::exists(path / "static" / "css" / "style.css") && fs::exists(path / "src" / "main.cpp");
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

std::optional<int> readIdFromPrefix(const std::string& path, const std::string& prefix) {
    if (path.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }

    const std::string rawId = path.substr(prefix.size());
    if (rawId.empty() || !std::all_of(rawId.begin(), rawId.end(), ::isdigit)) {
        return std::nullopt;
    }

    return std::stoi(rawId);
}

std::string makeRawResponse(const HttpResponse& response, bool includeBody) {
    std::ostringstream raw;
    raw << "HTTP/1.1 " << response.statusCode << ' ' << response.statusText << "\r\n";
    raw << "Content-Type: " << response.mimeType << "\r\n";
    raw << "Content-Length: " << response.body.size() << "\r\n";
    raw << "Connection: close\r\n";

    for (const auto& [name, value] : response.headers) {
        raw << name << ": " << value << "\r\n";
    }

    raw << "\r\n";
    if (includeBody) {
        raw << response.body;
    }

    return raw.str();
}

HttpResponse redirectTo(const std::string& location) {
    return {
        303,
        "See Other",
        "<h1>303 See Other</h1>",
        "text/html; charset=utf-8",
        {{"Location", location}}
    };
}

HttpResponse notFound(const std::string& message) {
    return {404, "Not Found", views::renderNotFoundPage(message), "text/html; charset=utf-8", {}};
}

HttpResponse serverError(const std::string& message) {
    return {500, "Internal Server Error", views::renderServerErrorPage(message), "text/html; charset=utf-8", {}};
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

bool parseRequestHead(const std::string& head, HttpRequest& request) {
    std::istringstream stream(head);
    std::string line;

    if (!std::getline(stream, line)) {
        return false;
    }

    line = trim(line);
    std::istringstream firstLine(line);
    firstLine >> request.method >> request.path >> request.version;

    if (request.method.empty() || request.path.empty()) {
        return false;
    }

    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        const std::string name = toLower(trim(line.substr(0, colon)));
        const std::string value = trim(line.substr(colon + 1));
        request.headers[name] = value;
    }

    return true;
}

bool readHttpRequest(int clientSocket, HttpRequest& request) {
    std::string raw;
    char buffer[BUFFER_SIZE] = {};

    while (raw.find("\r\n\r\n") == std::string::npos) {
        const ssize_t received = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            return false;
        }

        raw.append(buffer, static_cast<std::size_t>(received));
        if (raw.size() > MAX_REQUEST_SIZE) {
            return false;
        }
    }

    const std::size_t headerEnd = raw.find("\r\n\r\n");
    if (!parseRequestHead(raw.substr(0, headerEnd), request)) {
        return false;
    }

    int contentLength = 0;
    const auto contentLengthIt = request.headers.find("content-length");
    if (contentLengthIt != request.headers.end()) {
        contentLength = std::max(0, std::stoi(contentLengthIt->second));
    }

    const std::size_t bodyStart = headerEnd + 4;
    const std::size_t expectedSize = bodyStart + static_cast<std::size_t>(contentLength);

    while (raw.size() < expectedSize) {
        const ssize_t received = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            return false;
        }

        raw.append(buffer, static_cast<std::size_t>(received));
        if (raw.size() > MAX_REQUEST_SIZE) {
            return false;
        }
    }

    if (contentLength > 0) {
        request.body = raw.substr(bodyStart, static_cast<std::size_t>(contentLength));
    }

    return true;
}

int hexValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

std::string urlDecode(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());

    for (std::size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if (ch == '+') {
            decoded += ' ';
        } else if (ch == '%' && index + 2 < value.size()) {
            const int high = hexValue(value[index + 1]);
            const int low = hexValue(value[index + 2]);
            if (high >= 0 && low >= 0) {
                decoded += static_cast<char>(high * 16 + low);
                index += 2;
            }
        } else {
            decoded += ch;
        }
    }

    return decoded;
}

std::map<std::string, std::string> parseFormBody(const std::string& body) {
    std::map<std::string, std::string> fields;
    std::size_t start = 0;

    while (start <= body.size()) {
        const std::size_t end = body.find('&', start);
        const std::string pair = body.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const std::size_t equal = pair.find('=');

        if (equal != std::string::npos) {
            fields[urlDecode(pair.substr(0, equal))] = urlDecode(pair.substr(equal + 1));
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return fields;
}

int parsePrice(const std::string& value) {
    try {
        return std::max(0, std::stoi(value));
    } catch (...) {
        return 0;
    }
}

Item itemFromForm(const std::map<std::string, std::string>& form) {
    auto get = [&form](const std::string& name) {
        const auto it = form.find(name);
        return it == form.end() ? std::string{} : it->second;
    };

    Item item;
    item.name = get("name");
    item.hero = get("hero");
    item.rarity = get("rarity");
    item.type = get("type");
    item.price = parsePrice(get("price"));
    item.collection = get("collection");
    item.imageUrl = get("image_url");
    item.description = get("description");
    return item;
}

HttpResponse serveStaticFile(const fs::path& projectRoot, const std::string& path) {
    if (path.rfind("/static/", 0) != 0 || containsPathTraversal(path)) {
        return notFound("Страница не найдена.");
    }

    const std::string filePath = path.substr(1);
    const std::string body = readFile(projectRoot / filePath);
    if (body.empty()) {
        return notFound("Файл не найден.");
    }

    return {200, "OK", body, getMimeType(filePath), {}};
}

HttpResponse routeGet(Database& database, const fs::path& projectRoot, const std::string& rawPath) {
    const std::string path = stripQueryString(rawPath);

    if (path.rfind("/static/", 0) == 0) {
        return serveStaticFile(projectRoot, path);
    }
    if (path == "/" || path == "/index.html") {
        return {200, "OK", views::renderCatalogPage(database.getAllItems()), "text/html; charset=utf-8", {}};
    }
    if (const auto id = readIdFromPrefix(path, "/items/")) {
        const auto item = database.getItemById(*id);
        if (!item) {
            return notFound("Предмет не найден.");
        }
        return {200, "OK", views::renderItemPage(*item), "text/html; charset=utf-8", {}};
    }
    if (path == "/admin" || path == "/admin.html") {
        return {200, "OK", views::renderAdminPage(database.getAllItems()), "text/html; charset=utf-8", {}};
    }
    if (path == "/admin/new" || path == "/admin-form.html") {
        return {200, "OK", views::renderItemFormPage(std::nullopt), "text/html; charset=utf-8", {}};
    }
    if (const auto id = readIdFromPrefix(path, "/admin/edit/")) {
        const auto item = database.getItemById(*id);
        if (!item) {
            return notFound("Запись для редактирования не найдена.");
        }
        return {200, "OK", views::renderItemFormPage(item), "text/html; charset=utf-8", {}};
    }

    return notFound("Страница не найдена.");
}

HttpResponse routePost(Database& database, const std::string& rawPath, const std::string& body) {
    const std::string path = stripQueryString(rawPath);
    const auto form = parseFormBody(body);

    if (path == "/admin/create") {
        database.createItem(itemFromForm(form));
        return redirectTo("/admin");
    }
    if (const auto id = readIdFromPrefix(path, "/admin/update/")) {
        database.updateItem(*id, itemFromForm(form));
        return redirectTo("/admin");
    }
    if (const auto id = readIdFromPrefix(path, "/admin/delete/")) {
        database.deleteItem(*id);
        return redirectTo("/admin");
    }

    return notFound("Действие не найдено.");
}

HttpResponse routeRequest(Database& database, const fs::path& projectRoot, const HttpRequest& request) {
    try {
        if (request.method == "GET" || request.method == "HEAD") {
            return routeGet(database, projectRoot, request.path);
        }
        if (request.method == "POST") {
            return routePost(database, request.path, request.body);
        }
        return {405, "Method Not Allowed", "<h1>405 Method Not Allowed</h1>", "text/html; charset=utf-8", {}};
    } catch (const std::exception& error) {
        return serverError(error.what());
    }
}

void closeClient(int clientSocket) {
    shutdown(clientSocket, SHUT_WR);
    close(clientSocket);
}

void handleClient(int clientSocket, Database& database, const fs::path& projectRoot) {
    int enabled = 1;
    setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));

    timeval clientTimeout = {};
    clientTimeout.tv_sec = 3;
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &clientTimeout, sizeof(clientTimeout));
    setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, &clientTimeout, sizeof(clientTimeout));

    HttpRequest request;
    if (!readHttpRequest(clientSocket, request)) {
        closeClient(clientSocket);
        return;
    }

    const bool includeBody = request.method != "HEAD";
    const HttpResponse response = routeRequest(database, projectRoot, request);
    sendAll(clientSocket, makeRawResponse(response, includeBody));
    closeClient(clientSocket);
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

    try {
        Database database(projectRoot / "data" / "catalog.db");
        database.initialize();

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

            handleClient(clientSocket, database, projectRoot);
        }

        close(serverSocket);
        std::cout << "\nServer stopped" << std::endl;
    } catch (const std::exception& error) {
        std::cerr << "Application error: " << error.what() << std::endl;
        return 1;
    }

    return 0;
}

