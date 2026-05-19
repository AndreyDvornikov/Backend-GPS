#include "TelemetryServer.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "PayloadParser.h"

TelemetryServer::TelemetryServer(Database& database, TelemetryModel& model, std::atomic<bool>& running)
    : database_(database), model_(model), running_(running) {}

void TelemetryServer::Run() {
    const int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        return;
    }

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(kListenPort);

    if (bind(serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(serverFd);
        return;
    }
    if (listen(serverFd, 8) < 0) {
        close(serverFd);
        return;
    }

    while (running_.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(serverFd, &readSet);
        timeval tv{1, 0};
        if (select(serverFd + 1, &readSet, nullptr, nullptr, &tv) <= 0) {
            continue;
        }

        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        const int clientFd = accept(serverFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (clientFd >= 0) {
            std::thread(&TelemetryServer::HandleClient, this, clientFd).detach();
        }
    }

    close(serverFd);
}

bool TelemetryServer::ProcessPayloadLine(const std::string& line) {
    std::vector<DbRow> rows;
    LocationData location;
    long long timestampMs = 0;

    if (!ParsePayload(line, rows, location, timestampMs)) {
        model_.IncrementMalformedPackets();
        return false;
    }

    for (const auto& row : rows) {
        if (!database_.InsertRow(row)) {
            std::cerr << "Failed to persist sample" << std::endl;
        }
    }

    model_.AddSamples(rows, location, timestampMs);
    return true;
}

void TelemetryServer::HandleClient(int clientFd) {
    std::string pending;
    pending.reserve(8192);
    char buffer[4096];

    while (running_.load()) {
        const ssize_t count = recv(clientFd, buffer, sizeof(buffer), 0);
        if (count <= 0) {
            break;
        }

        pending.append(buffer, static_cast<size_t>(count));
        size_t pos = 0;
        while (true) {
            const size_t newline = pending.find('\n', pos);
            if (newline == std::string::npos) {
                pending.erase(0, pos);
                break;
            }

            std::string line = pending.substr(pos, newline - pos);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty()) {
                ProcessPayloadLine(line);
            }
            pos = newline + 1;
        }
    }

    close(clientFd);
}
