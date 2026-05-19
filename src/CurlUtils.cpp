#include "CurlUtils.h"

#include <cstdio>

#include <curl/curl.h>

namespace {
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userData) {
    const size_t total = size * nmemb;
    auto* buffer = static_cast<std::vector<unsigned char>*>(userData);
    const auto* begin = static_cast<unsigned char*>(contents);
    buffer->insert(buffer->end(), begin, begin + total);
    return total;
}
}

std::vector<unsigned char> DownloadBinary(const std::string& url) {
    std::vector<unsigned char> buffer;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return buffer;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "RadarTelemetry/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    const CURLcode result = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK) {
        std::printf("curl failed\n");
        buffer.clear();
    }

    return buffer;
}
