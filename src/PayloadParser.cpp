#include "PayloadParser.h"

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {
std::optional<int> GetOptionalInt(const json& obj, const char* key) {
    if (!obj.contains(key) || obj[key].is_null()) {
        return std::nullopt;
    }
    if (obj[key].is_number_integer()) {
        return obj[key].get<int>();
    }
    if (obj[key].is_number()) {
        return static_cast<int>(obj[key].get<double>());
    }
    return std::nullopt;
}

std::optional<long long> GetOptionalInt64(const json& obj, const char* key) {
    if (!obj.contains(key) || obj[key].is_null()) {
        return std::nullopt;
    }
    if (obj[key].is_number_integer()) {
        return obj[key].get<long long>();
    }
    if (obj[key].is_number()) {
        return static_cast<long long>(obj[key].get<double>());
    }
    return std::nullopt;
}

std::optional<double> GetOptionalDouble(const json& obj, const char* key) {
    if (!obj.contains(key) || obj[key].is_null()) {
        return std::nullopt;
    }
    if (obj[key].is_number()) {
        return obj[key].get<double>();
    }
    return std::nullopt;
}

std::optional<std::string> GetOptionalString(const json& obj, const char* key) {
    if (!obj.contains(key) || obj[key].is_null() || !obj[key].is_string()) {
        return std::nullopt;
    }
    return obj[key].get<std::string>();
}
}

bool ParsePayload(const std::string& line,
                  std::vector<DbRow>& rows,
                  LocationData& location,
                  long long& timestampMs) {
    rows.clear();

    json root;
    try {
        root = json::parse(line);
    } catch (...) {
        return false;
    }

    if (!root.contains("schema_version") || root["schema_version"] != 1) {
        return false;
    }
    if (!root.contains("timestamp_ms") || !root["timestamp_ms"].is_number_integer()) {
        return false;
    }
    if (!root.contains("location") || !root["location"].is_object()) {
        return false;
    }
    if (!root.contains("network_samples") || !root["network_samples"].is_array()) {
        return false;
    }

    timestampMs = root["timestamp_ms"].get<long long>();
    const auto& loc = root["location"];
    location.latitude = GetOptionalDouble(loc, "latitude");
    location.longitude = GetOptionalDouble(loc, "longitude");
    location.altitude = GetOptionalDouble(loc, "altitude");
    location.accuracy = GetOptionalDouble(loc, "accuracy");

    for (const auto& sample : root["network_samples"]) {
        if (!sample.is_object()) {
            continue;
        }
        if (!sample.contains("network_type") || !sample["network_type"].is_string()) {
            continue;
        }
        if (!sample.contains("identity") || !sample["identity"].is_object()) {
            continue;
        }
        if (!sample.contains("signal") || !sample["signal"].is_object()) {
            continue;
        }

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
