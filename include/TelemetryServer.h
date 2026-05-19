/// @file TelemetryServer.h
/// @brief TCP-сервер приёма телеметрии и передачи её в модель.

#pragma once

#include <atomic>
#include <string>

#include "Database.h"
#include "TelemetryModel.h"

/// @brief Сетевой сервис, принимающий телеметрические пакеты от клиентов.
class TelemetryServer {
public:
    /// @brief Создаёт сервер с зависимостями на БД, модель и флаг остановки.
    TelemetryServer(Database& database, TelemetryModel& model, std::atomic<bool>& running);

    /// @brief Запускает цикл прослушивания TCP-порта.
    void Run();

private:
    bool ProcessPayloadLine(const std::string& line);
    void HandleClient(int clientFd);

    Database& database_;
    TelemetryModel& model_;
    std::atomic<bool>& running_;
    static constexpr int kListenPort = 5555;
};
