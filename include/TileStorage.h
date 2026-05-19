/// @file TileStorage.h
/// @brief Работа с файловым кэшем тайлов OpenStreetMap.

#pragma once

#include <filesystem>
#include <vector>

/// @brief Определяет каталог сборки, в котором должен лежать кэш тайлов.
std::filesystem::path DetectTileCacheRoot();

/// @brief Возвращает путь к PNG-файлу тайла в файловом кэше.
std::filesystem::path GetTileCachePath(const std::filesystem::path& cacheRoot, int zoom, int x, int y);

/// @brief Загружает бинарный файл в память.
bool LoadBinaryFile(const std::filesystem::path& filePath, std::vector<unsigned char>& bytes);

/// @brief Сохраняет бинарный буфер на диск, создавая каталоги при необходимости.
bool SaveBinaryFile(const std::filesystem::path& filePath, const std::vector<unsigned char>& bytes);
