#include <GL/glew.h>
#include <SDL.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <curl/curl.h>
#include <vector>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>
#include <libpq-fe.h>
#include <future>
#include <deque>

#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"

using json = nlohmann::json;

namespace {

constexpr int kListenPort = 5555;
constexpr size_t kMaxSeriesPoints = 1200;
constexpr int kInvalidMetricIntMax = 2147483647;

struct LocationData {
    std::optional<double> latitude;
    std::optional<double> longitude;
    std::optional<double> altitude;
    std::optional<double> accuracy;
};

struct LiveSample {
    std::string networkType;
    std::optional<int> pci;
    std::optional<long long> cellIdentity;
    std::optional<int> rsrp;
    std::optional<int> rssi;
    std::optional<int> sinr;
};

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

struct DbRow {
    long long timestampMs = 0;
    std::optional<double> latitude;
    std::optional<double> longitude;
    std::optional<double> altitude;
    std::optional<double> accuracy;
    std::string networkType;
    bool registered = false;
    std::optional<int> band;
    std::optional<long long> cellIdentity;
    std::optional<long long> nci;
    std::optional<int> earfcn;
    std::optional<int> arfcn;
    std::optional<int> nrarfcn;
    std::optional<std::string> mcc;
    std::optional<std::string> mnc;
    std::optional<int> pci;
    std::optional<int> tac;
    std::optional<int> lac;
    std::optional<int> bsic;
    std::optional<int> psc;
    std::optional<int> asuLevel;
    std::optional<int> cqi;
    std::optional<int> rsrp;
    std::optional<int> rsrq;
    std::optional<int> rssi;
    std::optional<int> rssnr;
    std::optional<long long> timingAdvance;
    std::optional<int> ssRsrp;
    std::optional<int> ssRsrq;
    std::optional<int> ssSinr;
    std::string rawJson;
};

std::mutex gStateMutex;
std::atomic<bool> gRunning{true};
TelemetryState gState;
long long gLastPlotTimestampMs = 0;
float gMonotonicPlotTimeSec = 0.0f;

const char* EnvOrDefault(const char* key, const char* fallback) {
    const char* value = std::getenv(key);
    return (value != nullptr && std::strlen(value) > 0) ? value : fallback;
}

bool IsSafeDbName(const std::string& name) {
    if (name.empty()) return false;
    for (char ch : name) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) return false;
    }
    return true;
}

std::optional<int> GetOptionalInt(const json& obj, const char* key) {
    if (!obj.contains(key) || obj[key].is_null()) return std::nullopt;
    if (obj[key].is_number_integer()) return obj[key].get<int>();
    if (obj[key].is_number()) return static_cast<int>(obj[key].get<double>());
    return std::nullopt;
}

std::optional<long long> GetOptionalInt64(const json& obj, const char* key) {
    if (!obj.contains(key) || obj[key].is_null()) return std::nullopt;
    if (obj[key].is_number_integer()) return obj[key].get<long long>();
    if (obj[key].is_number()) return static_cast<long long>(obj[key].get<double>());
    return std::nullopt;
}

std::optional<double> GetOptionalDouble(const json& obj, const char* key) {
    if (!obj.contains(key) || obj[key].is_null()) return std::nullopt;
    if (obj[key].is_number()) return obj[key].get<double>();
    return std::nullopt;
}

std::optional<std::string> GetOptionalString(const json& obj, const char* key) {
    if (!obj.contains(key) || obj[key].is_null() || !obj[key].is_string()) return std::nullopt;
    return obj[key].get<std::string>();
}

// --------------- Database (оставлен без изменений) ---------------
class Database {
public:
    bool Init() {
        const std::string host = EnvOrDefault("PGHOST", "127.0.0.1");
        const std::string port = EnvOrDefault("PGPORT", "5432");
        const std::string user = EnvOrDefault("PGUSER", "postgres");
        const std::string pass = EnvOrDefault("PGPASSWORD", "postgres");
        dbName_ = EnvOrDefault("PGDATABASE", "radar_telemetry");

        if (!IsSafeDbName(dbName_)) {
            std::cerr << "Unsafe PGDATABASE name: " << dbName_ << std::endl;
            return false;
        }
        if (!EnsureDatabaseExists(host, port, user, pass, dbName_)) return false;

        const std::string connInfo = "host=" + host + " port=" + port + " user=" + user +
                                     " password=" + pass + " dbname=" + dbName_;
        conn_ = PQconnectdb(connInfo.c_str());
        if (PQstatus(conn_) != CONNECTION_OK) {
            std::cerr << "PostgreSQL connect failed: " << PQerrorMessage(conn_) << std::endl;
            return false;
        }
        std::cout << "PostgreSQL connected to database: " << dbName_ << std::endl;
        return CreateSchema();
    }

    ~Database() {
        if (conn_) { PQfinish(conn_); conn_ = nullptr; }
    }

    bool InsertRow(const DbRow& row);
    bool Ready() const { return conn_ != nullptr; }
    bool LoadLastRows(int limit, std::vector<DbRow>& rows);

private:
    bool EnsureDatabaseExists(const std::string& host, const std::string& port,
                              const std::string& user, const std::string& pass,
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

bool Database::InsertRow(const DbRow& row) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!conn_) return false;

    std::vector<std::string> values;
    std::vector<const char*> params;
    values.reserve(31);
    params.reserve(31);

    auto pushValue = [&](const std::string& value) {
        values.push_back(value);
        params.push_back(values.back().c_str());
    };
    auto pushNull = [&]() { params.push_back(nullptr); };

    pushValue(std::to_string(row.timestampMs));
    row.latitude ? pushValue(std::to_string(*row.latitude)) : pushNull();
    row.longitude ? pushValue(std::to_string(*row.longitude)) : pushNull();
    row.altitude ? pushValue(std::to_string(*row.altitude)) : pushNull();
    row.accuracy ? pushValue(std::to_string(*row.accuracy)) : pushNull();
    pushValue(row.networkType);
    pushValue(row.registered ? "true" : "false");
    row.band ? pushValue(std::to_string(*row.band)) : pushNull();
    row.cellIdentity ? pushValue(std::to_string(*row.cellIdentity)) : pushNull();
    row.nci ? pushValue(std::to_string(*row.nci)) : pushNull();
    row.earfcn ? pushValue(std::to_string(*row.earfcn)) : pushNull();
    row.arfcn ? pushValue(std::to_string(*row.arfcn)) : pushNull();
    row.nrarfcn ? pushValue(std::to_string(*row.nrarfcn)) : pushNull();
    row.mcc ? pushValue(*row.mcc) : pushNull();
    row.mnc ? pushValue(*row.mnc) : pushNull();
    row.pci ? pushValue(std::to_string(*row.pci)) : pushNull();
    row.tac ? pushValue(std::to_string(*row.tac)) : pushNull();
    row.lac ? pushValue(std::to_string(*row.lac)) : pushNull();
    row.bsic ? pushValue(std::to_string(*row.bsic)) : pushNull();
    row.psc ? pushValue(std::to_string(*row.psc)) : pushNull();
    row.asuLevel ? pushValue(std::to_string(*row.asuLevel)) : pushNull();
    row.cqi ? pushValue(std::to_string(*row.cqi)) : pushNull();
    row.rsrp ? pushValue(std::to_string(*row.rsrp)) : pushNull();
    row.rsrq ? pushValue(std::to_string(*row.rsrq)) : pushNull();
    row.rssi ? pushValue(std::to_string(*row.rssi)) : pushNull();
    row.rssnr ? pushValue(std::to_string(*row.rssnr)) : pushNull();
    row.timingAdvance ? pushValue(std::to_string(*row.timingAdvance)) : pushNull();
    row.ssRsrp ? pushValue(std::to_string(*row.ssRsrp)) : pushNull();
    row.ssRsrq ? pushValue(std::to_string(*row.ssRsrq)) : pushNull();
    row.ssSinr ? pushValue(std::to_string(*row.ssSinr)) : pushNull();
    pushValue(row.rawJson);

    static const char* sql =
        "INSERT INTO radar_samples ("
        "timestamp_ms, latitude, longitude, altitude, accuracy, "
        "network_type, registered, band, cell_identity, nci, earfcn, arfcn, nrarfcn, "
        "mcc, mnc, pci, tac, lac, bsic, psc, asu_level, cqi, rsrp, rsrq, rssi, rssnr, "
        "timing_advance, ss_rsrp, ss_rsrq, ss_sinr, raw_json"
        ") VALUES ("
        "$1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19,$20,$21,$22,$23,$24,$25,$26,$27,$28,$29,$30,$31::jsonb"
        ") RETURNING id;";

    PGresult* result = PQexecParams(conn_, sql, static_cast<int>(params.size()),
                                    nullptr, params.data(), nullptr, nullptr, 0);
    bool ok = PQresultStatus(result) == PGRES_TUPLES_OK && PQntuples(result) == 1;
    if (!ok) {
        std::cerr << "Insert failed: " << PQresultErrorMessage(result) << std::endl;
    }
    PQclear(result);
    return ok;
}

bool Database::LoadLastRows(int limit, std::vector<DbRow>& rows) {
    rows.clear();
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!conn_) return false;

    const int safeLimit = std::max(limit, 1);
    const std::string limitValue = std::to_string(safeLimit);
    const char* params[1] = { limitValue.c_str() };

    static const char* sql = "SELECT * FROM radar_samples ORDER BY id DESC LIMIT $1;";
    PGresult* result = PQexecParams(conn_, sql, 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        std::cerr << "Load failed: " << PQresultErrorMessage(result) << std::endl;
        PQclear(result);
        return false;
    }

    auto getOptionalIntField = [&](int rowIndex, const char* fieldName) -> std::optional<int> {
        int col = PQfnumber(result, fieldName);
        if (col < 0 || PQgetisnull(result, rowIndex, col)) return std::nullopt;
        return std::atoi(PQgetvalue(result, rowIndex, col));
    };
    auto getOptionalInt64Field = [&](int rowIndex, const char* fieldName) -> std::optional<long long> {
        int col = PQfnumber(result, fieldName);
        if (col < 0 || PQgetisnull(result, rowIndex, col)) return std::nullopt;
        return std::atoll(PQgetvalue(result, rowIndex, col));
    };
    auto getOptionalDoubleField = [&](int rowIndex, const char* fieldName) -> std::optional<double> {
        int col = PQfnumber(result, fieldName);
        if (col < 0 || PQgetisnull(result, rowIndex, col)) return std::nullopt;
        return std::atof(PQgetvalue(result, rowIndex, col));
    };
    auto getOptionalStringField = [&](int rowIndex, const char* fieldName) -> std::optional<std::string> {
        int col = PQfnumber(result, fieldName);
        if (col < 0 || PQgetisnull(result, rowIndex, col)) return std::nullopt;
        return std::string(PQgetvalue(result, rowIndex, col));
    };
    auto getRequiredStringField = [&](int rowIndex, const char* fieldName) -> std::string {
        int col = PQfnumber(result, fieldName);
        if (col < 0 || PQgetisnull(result, rowIndex, col)) return {};
        return std::string(PQgetvalue(result, rowIndex, col));
    };
    auto getRequiredBoolField = [&](int rowIndex, const char* fieldName) -> bool {
        int col = PQfnumber(result, fieldName);
        if (col < 0 || PQgetisnull(result, rowIndex, col)) return false;
        return PQgetvalue(result, rowIndex, col)[0] == 't';
    };

    int n = PQntuples(result);
    rows.reserve(n);
    for (int i = 0; i < n; ++i) {
        DbRow row;
        row.timestampMs = getOptionalInt64Field(i, "timestamp_ms").value_or(0);
        row.latitude = getOptionalDoubleField(i, "latitude");
        row.longitude = getOptionalDoubleField(i, "longitude");
        row.altitude = getOptionalDoubleField(i, "altitude");
        row.accuracy = getOptionalDoubleField(i, "accuracy");
        row.networkType = getRequiredStringField(i, "network_type");
        row.registered = getRequiredBoolField(i, "registered");
        row.band = getOptionalIntField(i, "band");
        row.cellIdentity = getOptionalInt64Field(i, "cell_identity");
        row.nci = getOptionalInt64Field(i, "nci");
        row.earfcn = getOptionalIntField(i, "earfcn");
        row.arfcn = getOptionalIntField(i, "arfcn");
        row.nrarfcn = getOptionalIntField(i, "nrarfcn");
        row.mcc = getOptionalStringField(i, "mcc");
        row.mnc = getOptionalStringField(i, "mnc");
        row.pci = getOptionalIntField(i, "pci");
        row.tac = getOptionalIntField(i, "tac");
        row.lac = getOptionalIntField(i, "lac");
        row.bsic = getOptionalIntField(i, "bsic");
        row.psc = getOptionalIntField(i, "psc");
        row.asuLevel = getOptionalIntField(i, "asu_level");
        row.cqi = getOptionalIntField(i, "cqi");
        row.rsrp = getOptionalIntField(i, "rsrp");
        row.rsrq = getOptionalIntField(i, "rsrq");
        row.rssi = getOptionalIntField(i, "rssi");
        row.rssnr = getOptionalIntField(i, "rssnr");
        row.timingAdvance = getOptionalInt64Field(i, "timing_advance");
        row.ssRsrp = getOptionalIntField(i, "ss_rsrp");
        row.ssRsrq = getOptionalIntField(i, "ss_rsrq");
        row.ssSinr = getOptionalIntField(i, "ss_sinr");
        row.rawJson = getRequiredStringField(i, "raw_json");
        rows.push_back(std::move(row));
    }
    PQclear(result);
    std::reverse(rows.begin(), rows.end());
    return true;
}

bool Database::EnsureDatabaseExists(const std::string& host, const std::string& port,
                                    const std::string& user, const std::string& pass,
                                    const std::string& dbName) {
    const std::string connInfo = "host=" + host + " port=" + port + " user=" + user +
                                 " password=" + pass + " dbname=postgres";
    PGconn* bootstrap = PQconnectdb(connInfo.c_str());
    if (PQstatus(bootstrap) != CONNECTION_OK) {
        std::cerr << "Bootstrap connect failed: " << PQerrorMessage(bootstrap) << std::endl;
        PQfinish(bootstrap);
        return false;
    }

    const char* values[1] = { dbName.c_str() };
    PGresult* check = PQexecParams(bootstrap, "SELECT 1 FROM pg_database WHERE datname = $1",
                                   1, nullptr, values, nullptr, nullptr, 0);
    bool exists = (PQresultStatus(check) == PGRES_TUPLES_OK && PQntuples(check) > 0);
    PQclear(check);

    if (!exists) {
        std::string createSql = "CREATE DATABASE " + dbName;
        PGresult* create = PQexec(bootstrap, createSql.c_str());
        if (PQresultStatus(create) != PGRES_COMMAND_OK) {
            std::cerr << "Create database failed: " << PQresultErrorMessage(create) << std::endl;
            PQclear(create);
            PQfinish(bootstrap);
            return false;
        }
        PQclear(create);
    }
    PQfinish(bootstrap);
    return true;
}

bool Database::CreateSchema() {
    PGresult* result = PQexec(conn_, kRadarSchemaSql);
    bool ok = PQresultStatus(result) == PGRES_COMMAND_OK;
    if (!ok) std::cerr << "Schema creation failed: " << PQresultErrorMessage(result) << std::endl;
    PQclear(result);
    return ok;
}

// ---------- Остальная телеметрия (без изменений) ----------
constexpr size_t kMaxPlotPoints = 400;

void PushPoint(std::vector<float>& xs, std::vector<float>& ys, float x, float y) {
    xs.push_back(x);
    ys.push_back(y);
    if (xs.size() > kMaxPlotPoints) {
        xs.erase(xs.begin());
        ys.erase(ys.begin());
    }
}

bool IsValidMetricValue(const std::optional<int>& value) {
    return value.has_value() && *value != -999 && *value != 0 && *value != kInvalidMetricIntMax;
}

void AppendMetricSample(std::unordered_map<int, std::vector<float>>& timeByPci,
                        std::unordered_map<int, std::vector<float>>& valueByPci,
                        int pci, float tSec, const std::optional<int>& value);

std::optional<int> SelectPreferredSinr(const DbRow& row) {
    if (IsValidMetricValue(row.ssSinr)) return row.ssSinr;
    if (IsValidMetricValue(row.rssnr)) return row.rssnr;
    return std::nullopt;
}

LiveSample BuildLiveSample(const DbRow& row) {
    LiveSample sample;
    sample.networkType = row.networkType;
    sample.pci = row.pci;
    sample.cellIdentity = row.cellIdentity;
    sample.rsrp = row.rsrp.has_value() ? row.rsrp : row.ssRsrp;
    sample.rssi = row.rssi;
    sample.sinr = SelectPreferredSinr(row);
    return sample;
}

void ClearTelemetryPlotBuffers(TelemetryState& state) {
    state.latestSamples.clear();
    state.rsrpTimeByPci.clear();
    state.rsrpValueByPci.clear();
    state.rssiTimeByPci.clear();
    state.rssiValueByPci.clear();
    state.sinrTimeByPci.clear();
    state.sinrValueByPci.clear();
}

void ApplyRowsToState(const std::vector<DbRow>& rows, const LocationData& location,
                      long long timestampMs, bool clearExisting) {
    std::vector<LiveSample> liveSamples;
    liveSamples.reserve(rows.size());
    for (const auto& row : rows) liveSamples.push_back(BuildLiveSample(row));

    std::lock_guard<std::mutex> lock(gStateMutex);
    if (clearExisting) {
        ClearTelemetryPlotBuffers(gState);
        gLastPlotTimestampMs = 0;
        gMonotonicPlotTimeSec = 0.0f;
    }
    gState.lastTimestampMs = timestampMs;
    gState.lastLocation = location;
    gState.latestSamples = std::move(liveSamples);

    for (const auto& row : rows) {
        if (!row.pci.has_value()) continue;
        if (gLastPlotTimestampMs == 0) {
            gMonotonicPlotTimeSec = 0.0f;
        } else {
            float deltaSec = static_cast<float>(row.timestampMs - gLastPlotTimestampMs) / 1000.0f;
            if (deltaSec <= 0.0f || deltaSec > 60.0f) deltaSec = 1.0f;
            gMonotonicPlotTimeSec += deltaSec;
        }
        gLastPlotTimestampMs = row.timestampMs;

        const std::optional<int> rsrp = row.rsrp.has_value() ? row.rsrp : row.ssRsrp;
        const std::optional<int> rssi = row.rssi;
        const std::optional<int> sinr = SelectPreferredSinr(row);
        AppendMetricSample(gState.rsrpTimeByPci, gState.rsrpValueByPci, *row.pci, gMonotonicPlotTimeSec, rsrp);
        AppendMetricSample(gState.rssiTimeByPci, gState.rssiValueByPci, *row.pci, gMonotonicPlotTimeSec, rssi);
        AppendMetricSample(gState.sinrTimeByPci, gState.sinrValueByPci, *row.pci, gMonotonicPlotTimeSec, sinr);
    }
}

bool LoadHistoryFromDb(Database& db, int sampleLimit) {
    std::vector<DbRow> rows;
    if (!db.LoadLastRows(sampleLimit, rows)) return false;

    LocationData location;
    long long timestampMs = 0;
    if (!rows.empty()) {
        const DbRow& last = rows.back();
        location.latitude = last.latitude;
        location.longitude = last.longitude;
        location.altitude = last.altitude;
        location.accuracy = last.accuracy;
        timestampMs = last.timestampMs;
    }
    ApplyRowsToState(rows, location, timestampMs, true);
    return true;
}

void AppendMetricSample(std::unordered_map<int, std::vector<float>>& timeByPci,
                        std::unordered_map<int, std::vector<float>>& valueByPci,
                        int pci, float tSec, const std::optional<int>& value) {
    if (!IsValidMetricValue(value)) return;
    auto& xs = timeByPci[pci];
    auto& ys = valueByPci[pci];
    PushPoint(xs, ys, tSec, static_cast<float>(*value));
}

bool ParsePayload(const std::string& line, std::vector<DbRow>& rows, LocationData& location, long long& timestampMs) {
    rows.clear();
    json root;
    try { root = json::parse(line); } catch (...) { return false; }

    if (!root.contains("schema_version") || root["schema_version"] != 1) return false;
    if (!root.contains("timestamp_ms") || !root["timestamp_ms"].is_number_integer()) return false;
    if (!root.contains("location") || !root["location"].is_object()) return false;
    if (!root.contains("network_samples") || !root["network_samples"].is_array()) return false;

    timestampMs = root["timestamp_ms"].get<long long>();
    const auto& loc = root["location"];
    location.latitude = GetOptionalDouble(loc, "latitude");
    location.longitude = GetOptionalDouble(loc, "longitude");
    location.altitude = GetOptionalDouble(loc, "altitude");
    location.accuracy = GetOptionalDouble(loc, "accuracy");

    for (const auto& sample : root["network_samples"]) {
        if (!sample.is_object()) continue;
        if (!sample.contains("network_type") || !sample["network_type"].is_string()) continue;
        if (!sample.contains("identity") || !sample["identity"].is_object()) continue;
        if (!sample.contains("signal") || !sample["signal"].is_object()) continue;

        const auto& identity = sample["identity"];
        const auto& signal = sample["signal"];

        DbRow row;
        row.timestampMs = timestampMs;
        row.latitude = location.latitude;
        row.longitude = location.longitude;
        row.altitude = location.altitude;
        row.accuracy = location.accuracy;
        row.networkType = sample["network_type"].get<std::string>();
        row.registered = sample.value("registered", false);

        row.band = GetOptionalInt(identity, "band");
        row.cellIdentity = GetOptionalInt64(identity, "cell_identity");
        row.nci = GetOptionalInt64(identity, "nci");
        row.earfcn = GetOptionalInt(identity, "earfcn");
        row.arfcn = GetOptionalInt(identity, "arfcn");
        row.nrarfcn = GetOptionalInt(identity, "nrarfcn");
        row.mcc = GetOptionalString(identity, "mcc");
        row.mnc = GetOptionalString(identity, "mnc");
        row.pci = GetOptionalInt(identity, "pci");
        row.tac = GetOptionalInt(identity, "tac");
        row.lac = GetOptionalInt(identity, "lac");
        row.bsic = GetOptionalInt(identity, "bsic");
        row.psc = GetOptionalInt(identity, "psc");

        row.asuLevel = GetOptionalInt(signal, "asu_level");
        row.cqi = GetOptionalInt(signal, "cqi");
        row.rsrp = GetOptionalInt(signal, "rsrp");
        row.rsrq = GetOptionalInt(signal, "rsrq");
        row.rssi = GetOptionalInt(signal, "rssi");
        row.rssnr = GetOptionalInt(signal, "rssnr");
        row.timingAdvance = GetOptionalInt64(signal, "timing_advance");
        row.ssRsrp = GetOptionalInt(signal, "ss_rsrp");
        row.ssRsrq = GetOptionalInt(signal, "ss_rsrq");
        row.ssSinr = GetOptionalInt(signal, "ss_sinr");

        row.rawJson = sample.dump();
        rows.push_back(std::move(row));
    }
    return true;
}

bool ProcessPayload(const std::string& line, Database& db) {
    std::vector<DbRow> rows;
    LocationData location;
    long long timestampMs = 0;

    if (!ParsePayload(line, rows, location, timestampMs)) {
        std::lock_guard<std::mutex> lock(gStateMutex);
        gState.malformedPackets += 1;
        return false;
    }

    size_t inserted = 0;
    for (const auto& row : rows) {
        if (db.InsertRow(row)) ++inserted;
        else std::cerr << "Failed to persist sample" << std::endl;
    }

    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        gState.receivedPackets += 1;
    }
    ApplyRowsToState(rows, location, timestampMs, false);
    return true;
}

void HandleClient(int clientFd, Database& db) {
    std::string pending;
    pending.reserve(8192);
    char buffer[4096];
    while (gRunning.load()) {
        ssize_t n = recv(clientFd, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        pending.append(buffer, static_cast<size_t>(n));
        size_t pos = 0;
        while (true) {
            size_t newline = pending.find('\n', pos);
            if (newline == std::string::npos) {
                pending.erase(0, pos);
                break;
            }
            std::string line = pending.substr(pos, newline - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) ProcessPayload(line, db);
            pos = newline + 1;
        }
    }
    close(clientFd);
}

void SocketListener(Database& db) {
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) return;
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

    while (gRunning.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(serverFd, &readSet);
        timeval tv{1, 0};
        if (select(serverFd + 1, &readSet, nullptr, nullptr, &tv) <= 0) continue;

        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = accept(serverFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (clientFd >= 0) std::thread(HandleClient, clientFd, std::ref(db)).detach();
    }
    close(serverFd);
}

void SetupCoffeeStyle() {
    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.94f, 0.97f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.13f, 0.17f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.17f, 0.22f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.19f, 0.22f, 0.29f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.19f, 0.24f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.22f, 0.26f, 0.34f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.35f, 0.45f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.22f, 0.26f, 0.36f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.12f, 0.13f, 0.17f, 1.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.16f, 0.18f, 0.23f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.35f, 0.40f, 0.52f, 0.70f);
    colors[ImGuiCol_Button] = ImVec4(0.23f, 0.40f, 0.75f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.32f, 0.50f, 0.85f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.34f, 0.66f, 1.00f);

    ImPlotStyle& plotStyle = ImPlot::GetStyle();
    plotStyle.Colors[ImPlotCol_PlotBg] = ImVec4(0.13f, 0.15f, 0.20f, 1.00f);
    plotStyle.Colors[ImPlotCol_FrameBg] = ImVec4(0.16f, 0.18f, 0.24f, 1.00f);
    plotStyle.Colors[ImPlotCol_PlotBorder] = ImVec4(0.42f, 0.47f, 0.60f, 0.85f);
    plotStyle.Colors[ImPlotCol_AxisText] = ImVec4(0.90f, 0.92f, 0.96f, 1.00f);
    plotStyle.Colors[ImPlotCol_AxisGrid] = ImVec4(0.65f, 0.70f, 0.82f, 0.20f);
    plotStyle.Colors[ImPlotCol_AxisTick] = ImVec4(0.80f, 0.84f, 0.92f, 0.80f);
    plotStyle.LineWeight = 2.0f;
}

void RenderPlot(const char* title,
                const std::unordered_map<int, std::vector<float>>& timeByPci,
                const std::unordered_map<int, std::vector<float>>& valueByPci) {
    if (!ImPlot::BeginPlot(title, ImVec2(-1, 220))) return;
    ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_None);
    ImPlot::SetupAxes("Time (s)", title);
    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.0f);

    int colorIndex = 0;
    for (const auto& entry : valueByPci) {
        int pci = entry.first;
        auto itTime = timeByPci.find(pci);
        if (itTime == timeByPci.end()) continue;
        const auto& ys = entry.second;
        const auto& xs = itTime->second;
        size_t count = std::min(xs.size(), ys.size());
        if (count == 0) continue;
        ImPlot::SetNextLineStyle(ImPlot::GetColormapColor(colorIndex++), 2.0f);
        ImPlot::PlotLine(("PCI " + std::to_string(pci)).c_str(), xs.data(), ys.data(), static_cast<int>(count));
    }
    ImPlot::PopStyleVar();
    ImPlot::EndPlot();
}

// ==================== Вспомогательные функции для карты ====================
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::vector<unsigned char>* buffer) {
    size_t total = size * nmemb;
    buffer->insert(buffer->end(), (unsigned char*)contents, (unsigned char*)contents + total);
    return total;
}

std::vector<unsigned char> DownloadTile(const std::string& url) {
    std::vector<unsigned char> buffer;
    CURL* curl = curl_easy_init();
    if (!curl) return buffer;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "RadarTelemetry/1.0");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) printf("curl failed\n");
    return buffer;
}

// ==================== Запуск GUI с интерактивной картой ====================
void RunGui(Database& db) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return;

    const char* glslVersion = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    SDL_Window* window = SDL_CreateWindow("Radar Backend",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1200, 840,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1);

    if (glewInit() != GLEW_OK) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

    SetupCoffeeStyle();
    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init(glslVersion);

    int loadSampleCount = 500;

    // ---------- Состояние карты ----------
    static double centerLat = 55.0, centerLon = 82.9; // Новосибирск
    static int zoom = 13;
    static const int MIN_ZOOM = 11;   // не отдалять дальше (весь город)
    static const int MAX_ZOOM = 18;
    static std::map<std::tuple<int,int,int>, GLuint> tileCache;

    // Вспомогательные функции преобразования координат
    auto LatLonToTileXY = [](double lat, double lon, int zoom, double& tileX, double& tileY) {
        double n = pow(2.0, zoom);
        tileX = (lon + 180.0) / 360.0 * n;
        double latRad = lat * M_PI / 180.0;
        tileY = (1.0 - log(tan(latRad) + 1.0 / cos(latRad)) / M_PI) / 2.0 * n;
    };

    auto TileXYToLatLon = [](double tileX, double tileY, int zoom, double& lat, double& lon) {
        double n = pow(2.0, zoom);
        lon = tileX / n * 360.0 - 180.0;
        double latRad = atan(sinh(M_PI * (1.0 - 2.0 * tileY / n)));
        lat = latRad * 180.0 / M_PI;
    };

    // ========== Асинхронная система загрузки тайлов ==========
    struct TileRequest { int z, x, y; };
    std::deque<TileRequest> pendingRequests;
    std::mutex requestMutex;

    struct LoadedTile {
        int z, x, y;
        std::vector<unsigned char> data;
    };
    std::deque<LoadedTile> readyTiles;
    std::mutex readyMutex;

    // Фоновый воркер – загружает тайлы из очереди запросов
    auto downloadWorker = [&]() {
        while (gRunning.load()) {
            TileRequest req;
            bool hasWork = false;
            {
                std::lock_guard<std::mutex> lock(requestMutex);
                if (!pendingRequests.empty()) {
                    req = pendingRequests.front();
                    pendingRequests.pop_front();
                    hasWork = true;
                }
            }
            if (!hasWork) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            std::string url = "https://tile.openstreetmap.org/" + std::to_string(req.z) + "/" +
                              std::to_string(req.x) + "/" + std::to_string(req.y) + ".png";
            auto data = DownloadTile(url);
            if (!data.empty()) {
                std::lock_guard<std::mutex> lock(readyMutex);
                readyTiles.push_back({req.z, req.x, req.y, std::move(data)});
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };

    std::thread worker(downloadWorker);
    worker.detach();

    // ---------- Основной цикл ----------
    while (gRunning.load()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) gRunning = false;
        }

        TelemetryState local;
        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            local = gState;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // === Окно телеметрии ===
        ImGui::Begin("Radar Telemetry");
        ImGui::Text("Packets: %lld (malformed: %lld)", local.receivedPackets, local.malformedPackets);
        ImGui::Text("DB ready: %s", local.dbReady ? "yes" : "no");
        ImGui::Text("Last timestamp_ms: %lld", local.lastTimestampMs);
        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputInt("Load N samples", &loadSampleCount);
        if (loadSampleCount < 1) loadSampleCount = 1;
        if (ImGui::Button("Load from DB")) LoadHistoryFromDb(db, loadSampleCount);

        if (local.lastLocation.latitude && local.lastLocation.longitude)
            ImGui::Text("Location: %.6f, %.6f", *local.lastLocation.latitude, *local.lastLocation.longitude);
        if (local.lastLocation.altitude) ImGui::Text("Altitude: %.2f", *local.lastLocation.altitude);
        if (local.lastLocation.accuracy) ImGui::Text("Accuracy: %.2f", *local.lastLocation.accuracy);

        if (ImGui::CollapsingHeader("Current samples", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable("samples", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Type");
                ImGui::TableSetupColumn("PCI");
                ImGui::TableSetupColumn("Cell ID");
                ImGui::TableSetupColumn("RSRP");
                ImGui::TableSetupColumn("RSSI");
                ImGui::TableSetupColumn("SINR");
                ImGui::TableHeadersRow();
                for (const auto& sample : local.latestSamples) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(sample.networkType.c_str());
                    ImGui::TableSetColumnIndex(1); ImGui::Text("%d", sample.pci.value_or(-1));
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%lld", sample.cellIdentity.value_or(-1));
                    ImGui::TableSetColumnIndex(3); ImGui::Text("%d", sample.rsrp.value_or(-999));
                    ImGui::TableSetColumnIndex(4); ImGui::Text("%d", sample.rssi.value_or(-999));
                    ImGui::TableSetColumnIndex(5); ImGui::Text("%d", sample.sinr.value_or(-999));
                }
                ImGui::EndTable();
            }
        }

        RenderPlot("RSRP", local.rsrpTimeByPci, local.rsrpValueByPci);
        RenderPlot("RSSI", local.rssiTimeByPci, local.rssiValueByPci);
        RenderPlot("SINR", local.sinrTimeByPci, local.sinrValueByPci);
        ImGui::End();

        // ==================== Окно карты ====================
        ImGui::SetNextWindowPos(ImVec2(10, 50), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
        ImGui::Begin("Map", nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

        ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        if (canvas_size.x < 100.0f) canvas_size.x = 100.0f;
        if (canvas_size.y < 100.0f) canvas_size.y = 100.0f;

        bool hovered = ImGui::IsWindowHovered();
        if (hovered) {
            // --- Зум колесиком ---
            if (io.MouseWheel != 0) {
                int newZoom = zoom + (io.MouseWheel > 0 ? 1 : -1);
                if (newZoom < MIN_ZOOM) newZoom = MIN_ZOOM;
                if (newZoom > MAX_ZOOM) newZoom = MAX_ZOOM;
                if (newZoom != zoom) {
                    ImVec2 mouse_pos = ImVec2(io.MousePos.x - canvas_p0.x, io.MousePos.y - canvas_p0.y);
                    double tileX_center, tileY_center;
                    LatLonToTileXY(centerLat, centerLon, zoom, tileX_center, tileY_center);
                    double tileX_mouse_old = tileX_center + (mouse_pos.x - canvas_size.x/2.0) / 256.0;
                    double tileY_mouse_old = tileY_center + (mouse_pos.y - canvas_size.y/2.0) / 256.0;
                    double geoLat, geoLon;
                    TileXYToLatLon(tileX_mouse_old, tileY_mouse_old, zoom, geoLat, geoLon);

                    zoom = newZoom;

                    double tileX_mouse_new, tileY_mouse_new;
                    LatLonToTileXY(geoLat, geoLon, zoom, tileX_mouse_new, tileY_mouse_new);
                    double newTileX_center = tileX_mouse_new - (mouse_pos.x - canvas_size.x/2.0) / 256.0;
                    double newTileY_center = tileY_mouse_new - (mouse_pos.y - canvas_size.y/2.0) / 256.0;
                    TileXYToLatLon(newTileX_center, newTileY_center, zoom, centerLat, centerLon);
                }
            }

            // --- Панорамирование (левая кнопка мыши) ---
            static bool dragging = false;
            static ImVec2 dragStart;
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (!dragging) {
                    dragging = true;
                    dragStart = io.MousePos;
                } else {
                    ImVec2 delta = ImVec2(io.MousePos.x - dragStart.x, io.MousePos.y - dragStart.y);
                    if (delta.x != 0.0f || delta.y != 0.0f) {
                        double tileX_center, tileY_center;
                        LatLonToTileXY(centerLat, centerLon, zoom, tileX_center, tileY_center);
                        tileX_center -= (double)delta.x / 256.0;
                        tileY_center -= (double)delta.y / 256.0;
                        TileXYToLatLon(tileX_center, tileY_center, zoom, centerLat, centerLon);
                        dragStart = io.MousePos;
                    }
                }
            } else {
                dragging = false;
            }
        }

        // === Вычисление области видимых тайлов ===
        double tileX_center, tileY_center;
        LatLonToTileXY(centerLat, centerLon, zoom, tileX_center, tileY_center);
        double tileX0 = tileX_center - (canvas_size.x / 2.0) / 256.0;
        double tileY0 = tileY_center - (canvas_size.y / 2.0) / 256.0;

        int startX = (int)floor(tileX0);
        int startY = (int)floor(tileY0);
        int endX = (int)floor(tileX0 + canvas_size.x / 256.0) + 1;
        int endY = (int)floor(tileY0 + canvas_size.y / 256.0) + 1;

        int maxTiles = (1 << zoom) - 1;
        if (startX < 0) startX = 0;
        if (startY < 0) startY = 0;
        if (endX > maxTiles) endX = maxTiles;
        if (endY > maxTiles) endY = maxTiles;

        // === Запрос недостающих тайлов (видимых) ===
        {
            std::lock_guard<std::mutex> lock(requestMutex);
            for (int y = startY; y <= endY; ++y) {
                for (int x = startX; x <= endX; ++x) {
                    if (tileCache.find(std::make_tuple(zoom, x, y)) == tileCache.end()) {
                        // Проверяем, нет ли уже такого запроса в очереди
                        bool already = false;
                        for (auto& r : pendingRequests)
                            if (r.z == zoom && r.x == x && r.y == y) { already = true; break; }
                        if (!already)
                            pendingRequests.push_back({zoom, x, y});
                    }
                }
            }
        }

        // === Обработка готовых тайлов (создание текстур) ===
        {
            std::lock_guard<std::mutex> lock(readyMutex);
            while (!readyTiles.empty()) {
                LoadedTile lt = std::move(readyTiles.front());
                readyTiles.pop_front();

                int w, h, channels;
                unsigned char* img = stbi_load_from_memory(lt.data.data(), lt.data.size(), &w, &h, &channels, 4);
                if (img) {
                    GLuint tex;
                    glGenTextures(1, &tex);
                    glBindTexture(GL_TEXTURE_2D, tex);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    stbi_image_free(img);
                    tileCache[std::make_tuple(lt.z, lt.x, lt.y)] = tex;
                }
            }
        }

        // === Отрисовка тайлов ===
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        for (int y = startY; y <= endY; ++y) {
            for (int x = startX; x <= endX; ++x) {
                auto it = tileCache.find(std::make_tuple(zoom, x, y));
                if (it == tileCache.end()) continue;

                GLuint tex = it->second;
                ImVec2 pos(canvas_p0.x + (x - tileX0) * 256.0f,
                           canvas_p0.y + (y - tileY0) * 256.0f);
                draw_list->AddImage((ImTextureID)(intptr_t)tex, pos, ImVec2(pos.x + 256, pos.y + 256));
            }
        }

        // Индикатор зума
        ImGui::SetCursorScreenPos(ImVec2(canvas_p0.x + 10, canvas_p0.y + 10));
        ImGui::TextColored(ImVec4(1,1,1,1), "Zoom: %d", zoom);

        ImGui::End();

        // --- Рендеринг ---
        ImGui::Render();
        glViewport(0, 0, static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y));
        glClearColor(0.10f, 0.10f, 0.10f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Очистка кеша тайлов
    for (auto& entry : tileCache)
        glDeleteTextures(1, &entry.second);
    tileCache.clear();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    }
}

int main() {
    Database db;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        gState.dbReady = db.Init();
    }

    std::thread listener(SocketListener, std::ref(db));
    RunGui(db);

    gRunning = false;
    if (listener.joinable()) listener.join();
    return 0;
}