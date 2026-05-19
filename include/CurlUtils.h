/// @file CurlUtils.h
/// @brief Вспомогательные функции для загрузки бинарных данных через libcurl.

#pragma once

#include <string>
#include <vector>

/// @brief Загружает содержимое URL в бинарный буфер.
std::vector<unsigned char> DownloadBinary(const std::string& url);
