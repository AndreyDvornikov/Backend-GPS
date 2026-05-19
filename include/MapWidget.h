/// @file MapWidget.h
/// @brief Виджет карты OpenStreetMap с асинхронной загрузкой и файловым кэшем тайлов.

#pragma once

#include <GL/glew.h>

#include <atomic>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "imgui.h"

#include "TelemetryTypes.h"

/// @brief Виджет отображения карты и управления тайлами OpenStreetMap.
class MapWidget {
public:
    /// @brief Создаёт виджет карты и запускает фоновые загрузчики.
    explicit MapWidget(std::atomic<bool>& running);

    /// @brief Освобождает OpenGL-ресурсы и завершает фоновые задачи.
    ~MapWidget();

    /// @brief Рисует окно карты и подгружает недостающие тайлы.
    void Render(const LocationData& location);

private:
    struct TileKey {
        int zoom = 0;
        int x = 0;
        int y = 0;

        bool operator<(const TileKey& other) const {
            if (zoom != other.zoom) {
                return zoom < other.zoom;
            }
            if (x != other.x) {
                return x < other.x;
            }
            return y < other.y;
        }
    };

    struct LoadedTile {
        TileKey key;
        std::vector<unsigned char> pngBytes;
        bool success = false;
    };

    void StartWorkers();
    void WorkerLoop();
    void UpdateCenterFromLocation(const LocationData& location);
    void QueueVisibleTiles(double tileX0, double tileY0, const ImVec2& canvasSize);
    void ProcessReadyTiles();
    void HandleMapInteraction(const ImVec2& plotPos, const ImVec2& plotSize);
    void UpdateCenterFromTilePosition(double tileX, double tileY);
    GLuint CreateTextureFromPng(const std::vector<unsigned char>& pngBytes);

    std::atomic<bool>& running_;
    std::filesystem::path cacheRoot_;
    std::map<TileKey, GLuint> tileTextures_;
    std::set<TileKey> inFlightTiles_;
    std::deque<TileKey> pendingRequests_;
    std::deque<LoadedTile> readyTiles_;
    std::mutex requestMutex_;
    std::mutex readyMutex_;
    std::vector<std::thread> workers_;
    double centerLat_ = 55.0;
    double centerLon_ = 82.9;
    bool followLocation_ = true;
    int zoom_ = 13;
    bool dragActive_ = false;
    ImVec2 dragStart_{};
    static constexpr int kTileSize = 256;
    static constexpr int kMinZoom = 11;
    static constexpr int kMaxZoom = 18;
    static constexpr int kWorkerCount = 4;
};
