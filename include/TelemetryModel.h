/// @file TelemetryModel.h
/// @brief Потокобезопасная модель телеметрии.

#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>

#include "TelemetryTypes.h"

class Database;

/// @brief Полный снимок телеметрии для GUI.
struct TelemetryState {
    long long lastTimestampMs = 0;
    LocationData lastLocation;
    std::vector<LiveSample> latestSamples;
    std::unordered_map<int, std::vector<float>> rsrpTimeByPci;
    std::unordered_map<int, std::vector<float>> rsrpValueByPci;
    std::unordered_map<int, std::vector<float>> rssiTimeByPci;
    std::unordered_map<int, std::vector<float>> rssiValueByPci;
    std::unordered_map<int, std::vector<float>> sinrTimeByPci;
    std::unordered_map<int, std::vector<float>> sinrValueByPci;
    long long receivedPackets = 0;
    long long malformedPackets = 0;
    bool dbReady = false;
};

/// @brief Потокобезопасное хранилище телеметрии для GUI и сетевого слоя.
class TelemetryModel {
public:
    TelemetryModel();

    /// @brief Добавляет валидные сэмплы в модель.
    void AddSamples(const std::vector<DbRow>& rows, const LocationData& loc, long long ts);

    /// @brief Загружает исторические записи из БД.
    bool LoadFromDatabase(Database& db, int sampleCount);

    /// @brief Возвращает консистентный снимок состояния.
    TelemetryState GetSnapshot() const;

    /// @brief Обновляет состояние готовности БД.
    void SetDatabaseReady(bool ready);

    /// @brief Увеличивает счётчик невалидных пакетов.
    void IncrementMalformedPackets();

private:
    mutable std::mutex mutex_;
    TelemetryState state_;
    long long lastPlotTimestampMs_ = 0;
    float monotonicPlotTimeSec_ = 0.0f;

    void clearState();
    float nextPlotTime(long long currentMs);
    void pushPoint(std::vector<float>& xs, std::vector<float>& ys, float x, float y);
};
