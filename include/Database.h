/// @file Database.h
/// @brief Интерфейс слоя хранения телеметрии в PostgreSQL.

#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <libpq-fe.h>

#include "TelemetryTypes.h"

/// @brief Репозиторий для инициализации и чтения телеметрических записей.
class Database {
public:
    Database() = default;
    ~Database();

    /// @brief Подключает БД и создаёт схему при необходимости.
    bool Init();

    /// @brief Сохраняет одну телеметрическую запись.
    bool InsertRow(const DbRow& row);

    /// @brief Возвращает признак активного подключения к БД.
    bool Ready() const;

    /// @brief Загружает последние записи из БД в порядке возрастания времени.
    bool LoadLastRows(int limit, std::vector<DbRow>& rows);

private:
    bool EnsureDatabaseExists(const std::string& host,
                              const std::string& port,
                              const std::string& user,
                              const std::string& pass,
                              const std::string& dbName);
    bool CreateSchema();

    PGconn* conn_ = nullptr;
    std::string dbName_;
    std::mutex dbMutex_;

    static constexpr const char* kRadarSchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS radar_samples (
    id BIGSERIAL PRIMARY KEY,
    received_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    timestamp_ms BIGINT NOT NULL,
    latitude DOUBLE PRECISION,
    longitude DOUBLE PRECISION,
    altitude DOUBLE PRECISION,
    accuracy DOUBLE PRECISION,
    network_type TEXT NOT NULL,
    registered BOOLEAN NOT NULL,
    band INTEGER,
    cell_identity BIGINT,
    nci BIGINT,
    earfcn INTEGER,
    arfcn INTEGER,
    nrarfcn INTEGER,
    mcc TEXT,
    mnc TEXT,
    pci INTEGER,
    tac INTEGER,
    lac INTEGER,
    bsic INTEGER,
    psc INTEGER,
    asu_level INTEGER,
    cqi INTEGER,
    rsrp INTEGER,
    rsrq INTEGER,
    rssi INTEGER,
    rssnr INTEGER,
    timing_advance BIGINT,
    ss_rsrp INTEGER,
    ss_rsrq INTEGER,
    ss_sinr INTEGER,
    raw_json JSONB NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_radar_samples_ts ON radar_samples(timestamp_ms);
CREATE INDEX IF NOT EXISTS idx_radar_samples_pci ON radar_samples(pci);
)SQL";
};
