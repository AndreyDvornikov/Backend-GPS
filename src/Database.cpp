#include "Database.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>

namespace {
const char* EnvOrDefault(const char* key, const char* fallback) {
    const char* value = std::getenv(key);
    return (value != nullptr && std::strlen(value) > 0) ? value : fallback;
}

bool IsSafeDbName(const std::string& name) {
    if (name.empty()) {
        return false;
    }

    for (char ch : name) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) {
            return false;
        }
    }

    return true;
}
}

Database::~Database() {
    if (conn_ != nullptr) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

bool Database::Init() {
    const std::string host = EnvOrDefault("PGHOST", "127.0.0.1");
    const std::string port = EnvOrDefault("PGPORT", "5432");
    const std::string user = EnvOrDefault("PGUSER", "postgres");
    const std::string pass = EnvOrDefault("PGPASSWORD", "postgres");
    dbName_ = EnvOrDefault("PGDATABASE", "radar_telemetry");

    if (!IsSafeDbName(dbName_)) {
        std::cerr << "Unsafe PGDATABASE name: " << dbName_ << std::endl;
        return false;
    }

    if (!EnsureDatabaseExists(host, port, user, pass, dbName_)) {
        return false;
    }

    const std::string connInfo =
        "host=" + host + " port=" + port + " user=" + user + " password=" + pass + " dbname=" + dbName_;
    conn_ = PQconnectdb(connInfo.c_str());
    if (PQstatus(conn_) != CONNECTION_OK) {
        std::cerr << "PostgreSQL connect failed: " << PQerrorMessage(conn_) << std::endl;
        return false;
    }

    std::cout << "PostgreSQL connected to database: " << dbName_ << std::endl;
    return CreateSchema();
}

bool Database::InsertRow(const DbRow& row) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (conn_ == nullptr) {
        return false;
    }

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

    PGresult* result = PQexecParams(
        conn_, sql, static_cast<int>(params.size()), nullptr, params.data(), nullptr, nullptr, 0);
    const bool ok = PQresultStatus(result) == PGRES_TUPLES_OK && PQntuples(result) == 1;
    if (!ok) {
        std::cerr << "Insert failed: " << PQresultErrorMessage(result) << std::endl;
    }
    PQclear(result);
    return ok;
}

bool Database::Ready() const {
    return conn_ != nullptr;
}

bool Database::LoadLastRows(int limit, std::vector<DbRow>& rows) {
    rows.clear();
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (conn_ == nullptr) {
        return false;
    }

    const int safeLimit = std::max(limit, 1);
    const std::string limitValue = std::to_string(safeLimit);
    const char* params[1] = {limitValue.c_str()};

    static const char* sql = "SELECT * FROM radar_samples ORDER BY id DESC LIMIT $1;";
    PGresult* result = PQexecParams(conn_, sql, 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        std::cerr << "Load failed: " << PQresultErrorMessage(result) << std::endl;
        PQclear(result);
        return false;
    }

    auto getOptionalIntField = [&](int rowIndex, const char* fieldName) -> std::optional<int> {
        const int col = PQfnumber(result, fieldName);
        if (col < 0 || PQgetisnull(result, rowIndex, col)) {
            return std::nullopt;
        }
        return std::atoi(PQgetvalue(result, rowIndex, col));
    };
    auto getOptionalInt64Field = [&](int rowIndex, const char* fieldName) -> std::optional<long long> {
        const int col = PQfnumber(result, fieldName);
        if (col < 0 || PQgetisnull(result, rowIndex, col)) {
            return std::nullopt;
        }
        return std::atoll(PQgetvalue(result, rowIndex, col));
    };
    auto getOptionalDoubleField = [&](int rowIndex, const char* fieldName) -> std::optional<double> {
        const int col = PQfnumber(result, fieldName);
        if (col < 0 || PQgetisnull(result, rowIndex, col)) {
            return std::nullopt;
        }
        return std::atof(PQgetvalue(result, rowIndex, col));
    };
    auto getOptionalStringField = [&](int rowIndex, const char* fieldName) -> std::optional<std::string> {
        const int col = PQfnumber(result, fieldName);
        if (col < 0 || PQgetisnull(result, rowIndex, col)) {
            return std::nullopt;
        }
        return std::string(PQgetvalue(result, rowIndex, col));
    };
    auto getRequiredStringField = [&](int rowIndex, const char* fieldName) -> std::string {
        const int col = PQfnumber(result, fieldName);
        if (col < 0 || PQgetisnull(result, rowIndex, col)) {
            return {};
        }
        return std::string(PQgetvalue(result, rowIndex, col));
    };
    auto getRequiredBoolField = [&](int rowIndex, const char* fieldName) -> bool {
        const int col = PQfnumber(result, fieldName);
        if (col < 0 || PQgetisnull(result, rowIndex, col)) {
            return false;
        }
        return PQgetvalue(result, rowIndex, col)[0] == 't';
    };

    const int count = PQntuples(result);
    rows.reserve(count);
    for (int i = 0; i < count; ++i) {
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

bool Database::EnsureDatabaseExists(const std::string& host,
                                    const std::string& port,
                                    const std::string& user,
                                    const std::string& pass,
                                    const std::string& dbName) {
    const std::string connInfo =
        "host=" + host + " port=" + port + " user=" + user + " password=" + pass + " dbname=postgres";
    PGconn* bootstrap = PQconnectdb(connInfo.c_str());
    if (PQstatus(bootstrap) != CONNECTION_OK) {
        std::cerr << "Bootstrap connect failed: " << PQerrorMessage(bootstrap) << std::endl;
        PQfinish(bootstrap);
        return false;
    }

    const char* values[1] = {dbName.c_str()};
    PGresult* check = PQexecParams(
        bootstrap, "SELECT 1 FROM pg_database WHERE datname = $1", 1, nullptr, values, nullptr, nullptr, 0);
    const bool exists = PQresultStatus(check) == PGRES_TUPLES_OK && PQntuples(check) > 0;
    PQclear(check);

    if (!exists) {
        const std::string createSql = "CREATE DATABASE " + dbName;
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
    const bool ok = PQresultStatus(result) == PGRES_COMMAND_OK;
    if (!ok) {
        std::cerr << "Schema creation failed: " << PQresultErrorMessage(result) << std::endl;
    }
    PQclear(result);
    return ok;
}
