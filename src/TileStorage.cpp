#include "TileStorage.h"

#include <fstream>

std::filesystem::path DetectTileCacheRoot() {
    const auto current = std::filesystem::current_path();
    if (std::filesystem::exists(current / "CMakeCache.txt")) {
        return current;
    }
    if (std::filesystem::exists(current / "build" / "CMakeCache.txt")) {
        return current / "build";
    }
    if (std::filesystem::exists(current / "Backend-GPS" / "build" / "CMakeCache.txt")) {
        return current / "Backend-GPS" / "build";
    }
    return current / "build";
}

std::filesystem::path GetTileCachePath(const std::filesystem::path& cacheRoot, int zoom, int x, int y) {
    return cacheRoot / std::to_string(zoom) / std::to_string(x) / (std::to_string(y) + ".png");
}

bool LoadBinaryFile(const std::filesystem::path& filePath, std::vector<unsigned char>& bytes) {
    bytes.clear();
    std::ifstream input(filePath, std::ios::binary);
    if (!input) {
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamsize size = input.tellg();
    if (size <= 0) {
        return false;
    }
    input.seekg(0, std::ios::beg);

    bytes.resize(static_cast<size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    return input.good() || input.eof();
}

bool SaveBinaryFile(const std::filesystem::path& filePath, const std::vector<unsigned char>& bytes) {
    std::error_code error;
    std::filesystem::create_directories(filePath.parent_path(), error);
    if (error) {
        return false;
    }

    std::ofstream output(filePath, std::ios::binary);
    if (!output) {
        return false;
    }

    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}
