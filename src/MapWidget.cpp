#include "MapWidget.h"

#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include "CurlUtils.h"
#include "TileStorage.h"
#include "imgui.h"
#include "implot.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {
void LatLonToTileXY(double lat, double lon, int zoom, double& tileX, double& tileY) {
    const double scale = std::pow(2.0, zoom);
    tileX = (lon + 180.0) / 360.0 * scale;
    const double latRad = lat * M_PI / 180.0;
    tileY = (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * scale;
}

void TileXYToLatLon(double tileX, double tileY, int zoom, double& lat, double& lon) {
    const double scale = std::pow(2.0, zoom);
    lon = tileX / scale * 360.0 - 180.0;
    const double latRad = std::atan(std::sinh(M_PI * (1.0 - 2.0 * tileY / scale)));
    lat = latRad * 180.0 / M_PI;
}
}

MapWidget::MapWidget(std::atomic<bool>& running)
    : running_(running), cacheRoot_(DetectTileCacheRoot()) {
    StartWorkers();
}

MapWidget::~MapWidget() {
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    for (auto& entry : tileTextures_) {
        glDeleteTextures(1, &entry.second);
    }
}

void MapWidget::Render(const LocationData& location) {
    UpdateCenterFromLocation(location);

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(1024, 960), ImGuiCond_FirstUseEver);
    ImGui::Begin("Map", nullptr, ImGuiWindowFlags_NoMove);
    ImGui::Checkbox("Follow GPS", &followLocation_);
    ImGui::SameLine();
    ImGui::Text("Zoom: %d", zoom_);

    const ImVec2 plotSize = ImGui::GetContentRegionAvail();
    if (plotSize.x < 100.0f || plotSize.y < 100.0f) {
        ImGui::End();
        return;
    }

    double centerTileX = 0.0;
    double centerTileY = 0.0;
    LatLonToTileXY(centerLat_, centerLon_, zoom_, centerTileX, centerTileY);

    const double visibleTilesX = plotSize.x / static_cast<double>(kTileSize);
    const double visibleTilesY = plotSize.y / static_cast<double>(kTileSize);
    const double tileX0 = centerTileX - visibleTilesX / 2.0;
    const double tileY0 = centerTileY - visibleTilesY / 2.0;
    const double xMin = tileX0;
    const double xMax = tileX0 + visibleTilesX;
    const double yMin = tileY0;
    const double yMax = tileY0 + visibleTilesY;

    const ImPlotFlags plotFlags = ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoMouseText | ImPlotFlags_NoBoxSelect;
    const ImPlotAxisFlags axisFlags = ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_Lock;
    if (ImPlot::BeginPlot("##MapPlot", plotSize, plotFlags)) {
        ImPlot::SetupAxes(nullptr, nullptr, axisFlags, axisFlags);
        ImPlot::SetupAxisLimits(ImAxis_X1, xMin, xMax, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, yMin, yMax, ImGuiCond_Always);

        QueueVisibleTiles(tileX0, tileY0, plotSize);
        ProcessReadyTiles();
        const ImVec2 plotPos = ImPlot::GetPlotPos();
        const int maxTileIndex = (1 << zoom_) - 1;
        const int startX = std::max(0, static_cast<int>(std::floor(xMin)));
        const int startY = std::max(0, static_cast<int>(std::floor(yMin)));
        const int endX = std::min(maxTileIndex, static_cast<int>(std::floor(xMax)) + 1);
        const int endY = std::min(maxTileIndex, static_cast<int>(std::floor(yMax)) + 1);

        for (int y = startY; y <= endY; ++y) {
            for (int x = startX; x <= endX; ++x) {
                const TileKey key{zoom_, x, y};
                const auto it = tileTextures_.find(key);
                if (it == tileTextures_.end()) {
                    continue;
                }

                const std::string label = "tile_" + std::to_string(zoom_) + "_" + std::to_string(x) + "_" + std::to_string(y);
                ImDrawList* drawList = ImGui::GetWindowDrawList();

                const float screenX =
                    plotPos.x + static_cast<float>((x - tileX0) * kTileSize);

                const float screenY =
                    plotPos.y + static_cast<float>((y - tileY0) * kTileSize);

                drawList->AddImage(
                    (ImTextureID)(intptr_t)it->second,
                    ImVec2(screenX, screenY),
                    ImVec2(screenX + kTileSize, screenY + kTileSize)
                );
            }
        }

        if (location.latitude && location.longitude) {
            double markerTileX = 0.0;
            double markerTileY = 0.0;
            LatLonToTileXY(*location.latitude, *location.longitude, zoom_, markerTileX, markerTileY);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 6.0f, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            ImPlot::PlotScatter("Current position", &markerTileX, &markerTileY, 1);
        }

        HandleMapInteraction(ImPlot::GetPlotPos(), ImPlot::GetPlotSize());
        ImPlot::EndPlot();
    }

    ImGui::End();
}

void MapWidget::StartWorkers() {
    workers_.reserve(kWorkerCount);
    for (int i = 0; i < kWorkerCount; ++i) {
        workers_.emplace_back(&MapWidget::WorkerLoop, this);
    }
}

void MapWidget::WorkerLoop() {
    while (running_.load()) {
        TileKey key;
        bool hasWork = false;
        {
            std::lock_guard<std::mutex> lock(requestMutex_);
            if (!pendingRequests_.empty()) {
                key = pendingRequests_.front();
                pendingRequests_.pop_front();
                hasWork = true;
            }
        }

        if (!hasWork) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        LoadedTile loaded;
        loaded.key = key;
        const auto filePath = GetTileCachePath(cacheRoot_, key.zoom, key.x, key.y);
        if (LoadBinaryFile(filePath, loaded.pngBytes)) {
            loaded.success = !loaded.pngBytes.empty();
        } else {
            const std::string url = "https://tile.openstreetmap.org/" + std::to_string(key.zoom) + "/" +
                                    std::to_string(key.x) + "/" + std::to_string(key.y) + ".png";
            loaded.pngBytes = DownloadBinary(url);
            loaded.success = !loaded.pngBytes.empty();
            if (loaded.success) {
                SaveBinaryFile(filePath, loaded.pngBytes);
            }
        }

        std::lock_guard<std::mutex> lock(readyMutex_);
        readyTiles_.push_back(std::move(loaded));
    }
}

void MapWidget::UpdateCenterFromLocation(const LocationData& location) {
    if (!followLocation_) {
        return;
    }
    if (location.latitude) {
        centerLat_ = *location.latitude;
    }
    if (location.longitude) {
        centerLon_ = *location.longitude;
    }
}

void MapWidget::QueueVisibleTiles(double tileX0, double tileY0, const ImVec2& canvasSize) {
    const int maxTileIndex = (1 << zoom_) - 1;
    const int startX = std::max(0, static_cast<int>(std::floor(tileX0)));
    const int startY = std::max(0, static_cast<int>(std::floor(tileY0)));
    const int endX = std::min(maxTileIndex, static_cast<int>(std::floor(tileX0 + canvasSize.x / kTileSize)) + 1);
    const int endY = std::min(maxTileIndex, static_cast<int>(std::floor(tileY0 + canvasSize.y / kTileSize)) + 1);

    std::lock_guard<std::mutex> lock(requestMutex_);
    for (int y = startY; y <= endY; ++y) {
        for (int x = startX; x <= endX; ++x) {
            const TileKey key{zoom_, x, y};
            if (tileTextures_.find(key) != tileTextures_.end()) {
                continue;
            }
            if (inFlightTiles_.find(key) != inFlightTiles_.end()) {
                continue;
            }

            inFlightTiles_.insert(key);
            pendingRequests_.push_back(key);
        }
    }
}

void MapWidget::ProcessReadyTiles() {
    std::lock_guard<std::mutex> lock(readyMutex_);
    while (!readyTiles_.empty()) {
        LoadedTile tile = std::move(readyTiles_.front());
        readyTiles_.pop_front();
        inFlightTiles_.erase(tile.key);
        if (!tile.success) {
            continue;
        }

        const GLuint texture = CreateTextureFromPng(tile.pngBytes);
        if (texture != 0) {
            tileTextures_[tile.key] = texture;
        }
    }
}

void MapWidget::HandleMapInteraction(const ImVec2& plotPos, const ImVec2& plotSize) {
    ImGuiIO& io = ImGui::GetIO();
    if (!ImPlot::IsPlotHovered()) {
        dragActive_ = false;
        return;
    }

    if (io.MouseWheel != 0.0f) {
        int newZoom = zoom_ + (io.MouseWheel > 0.0f ? 1 : -1);
        newZoom = std::max(kMinZoom, std::min(kMaxZoom, newZoom));
        if (newZoom != zoom_) {
            followLocation_ = false;
            const ImVec2 mousePos(io.MousePos.x - plotPos.x, io.MousePos.y - plotPos.y);
            double tileXCenter = 0.0;
            double tileYCenter = 0.0;
            LatLonToTileXY(centerLat_, centerLon_, zoom_, tileXCenter, tileYCenter);
            const double tileXMouseOld = tileXCenter + (mousePos.x - plotSize.x / 2.0f) / kTileSize;
            const double tileYMouseOld = tileYCenter + (mousePos.y - plotSize.y / 2.0f) / kTileSize;

            double geoLat = 0.0;
            double geoLon = 0.0;
            TileXYToLatLon(tileXMouseOld, tileYMouseOld, zoom_, geoLat, geoLon);

            zoom_ = newZoom;

            double tileXMouseNew = 0.0;
            double tileYMouseNew = 0.0;
            LatLonToTileXY(geoLat, geoLon, zoom_, tileXMouseNew, tileYMouseNew);
            const double newTileXCenter = tileXMouseNew - (mousePos.x - plotSize.x / 2.0f) / kTileSize;
            const double newTileYCenter = tileYMouseNew - (mousePos.y - plotSize.y / 2.0f) / kTileSize;
            UpdateCenterFromTilePosition(newTileXCenter, newTileYCenter);
        }
    }

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!dragActive_) {
            dragActive_ = true;
            dragStart_ = io.MousePos;
            followLocation_ = false;
        } else {
            const ImVec2 delta(io.MousePos.x - dragStart_.x, io.MousePos.y - dragStart_.y);
            if (delta.x != 0.0f || delta.y != 0.0f) {
                double tileXCenter = 0.0;
                double tileYCenter = 0.0;
                LatLonToTileXY(centerLat_, centerLon_, zoom_, tileXCenter, tileYCenter);
                tileXCenter -= static_cast<double>(delta.x) / kTileSize;
                tileYCenter -= static_cast<double>(delta.y) / kTileSize;
                UpdateCenterFromTilePosition(tileXCenter, tileYCenter);
                dragStart_ = io.MousePos;
            }
        }
    } else {
        dragActive_ = false;
    }
}

void MapWidget::UpdateCenterFromTilePosition(double tileX, double tileY) {
    TileXYToLatLon(tileX, tileY, zoom_, centerLat_, centerLon_);
}

GLuint MapWidget::CreateTextureFromPng(const std::vector<unsigned char>& pngBytes) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* image = stbi_load_from_memory(
        pngBytes.data(), static_cast<int>(pngBytes.size()), &width, &height, &channels, 4);
    if (image == nullptr) {
        return 0;
    }

    GLuint texture = 0;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        image
    );

    stbi_image_free(image);

    return texture;
}