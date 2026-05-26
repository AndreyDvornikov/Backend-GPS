#pragma once

#include <cstdint>
#include <vector>

/// @brief Метрика, по которой строится тепловая карта.
///
/// Используется для:
/// - выбора цветовой схемы;
/// - выбора поля телеметрии;
/// - отображения в ImGui.
enum class HeatMapMetric {
    RSRP,
    RSRQ,
    RSSI,
    Altitude
};

/// @brief Одна измеренная точка телеметрии.
///
/// Обычно формируется из записи БД.
struct HeatPoint {
    /// Географическая широта.
    double latitude;

    /// Географическая долгота.
    double longitude;

    /// Значение метрики.
    double value;
};

/// @brief Один RGBA-пиксель.
///
/// Используется при генерации heatmap image buffer.
struct RGBA {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

struct GeoBounds {
    double minLat = 0.0;
    double maxLat = 0.0;
    double minLon = 0.0;
    double maxLon = 0.0;
};

/// @brief Генератор тепловых карт.
///
/// Отвечает только за:
/// - интерполяцию;
/// - генерацию RGBA-буфера.
///
/// Не содержит:
/// - OpenGL;
/// - ImGui;
/// - PNG;
/// - threading;
/// - database logic.
class HeatMap {
public:
    static constexpr int kTileResolution = 256;

    /// @brief Выполняет IDW-интерполяцию значения в точке.
    ///
    /// Алгоритм Inverse Distance Weighting:
    /// ближайшие точки влияют сильнее дальних.
    ///
    /// @param lat Широта вычисляемой точки.
    /// @param lon Долгота вычисляемой точки.
    /// @param points Набор измеренных точек.
    /// @param radiusMeters Максимальный радиус поиска соседних точек.
    /// @param power Степень весовой функции.
    ///
    /// @return Интерполированное значение.
    static double IDWInterpolate(
        double lat,
        double lon,
        const std::vector<HeatPoint>& points,
        double radiusMeters,
        double power
    );

    /// @brief Возвращает географические границы OSM-тайла z/x/y.
    static GeoBounds GetTileBounds(int zoom, int x, int y);

    /// @brief Генерирует RGBA-буфер тепловой карты для OSM tile bounds.
    static void GenerateTileForMapTile(
        const std::vector<HeatPoint>& points,
        int zoom,
        int x,
        int y,
        HeatMapMetric metric,
        std::vector<uint8_t>& outRGBA
    );

    /// @brief Генерирует RGBA-буфер тепловой карты.
    ///
    /// Генерация выполняется для прямоугольной области:
    /// - min/max latitude;
    /// - min/max longitude.
    ///
    /// Результат сохраняется в outRGBA.
    ///
    /// @param points Набор измеренных точек.
    /// @param width Ширина изображения.
    /// @param height Высота изображения.
    /// @param minLat Минимальная широта области.
    /// @param maxLat Максимальная широта области.
    /// @param minLon Минимальная долгота области.
    /// @param maxLon Максимальная долгота области.
    /// @param metric Выбранная метрика.
    /// @param outRGBA Выходной RGBA-buffer.
    static void GenerateTile(
        const std::vector<HeatPoint>& points,
        int width,
        int height,
        double minLat,
        double maxLat,
        double minLon,
        double maxLon,
        HeatMapMetric metric,
        std::vector<uint8_t>& outRGBA
    );
};
