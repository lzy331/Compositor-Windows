#include "core/Types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstdio>

namespace compositor {

uint64_t physicalMemoryBytes() {
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) return status.ullTotalPhys;
    return 0;
#else
    return 8ull * 1024 * 1024 * 1024;
#endif
}

int64_t DocumentLimits::documentPixelBudget() {
    // A quarter of physical memory at 4 bytes a pixel, never less than one surface, never more
    // than 800 MP — exactly the rule in DocumentLimits.swift.
    int64_t quarter = static_cast<int64_t>(physicalMemoryBytes() / 16);
    return std::min<int64_t>(800'000'000, std::max<int64_t>(maxSurfacePixels, quarter));
}

Uuid Uuid::generate() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    Uuid id;
    uint64_t hi = dist(rng);
    uint64_t lo = dist(rng);
    for (int i = 0; i < 8; ++i) id.bytes[i] = static_cast<uint8_t>(hi >> (56 - 8 * i));
    for (int i = 0; i < 8; ++i) id.bytes[8 + i] = static_cast<uint8_t>(lo >> (56 - 8 * i));
    // RFC 4122: version 4 and the variant bits.
    id.bytes[6] = static_cast<uint8_t>((id.bytes[6] & 0x0F) | 0x40);
    id.bytes[8] = static_cast<uint8_t>((id.bytes[8] & 0x3F) | 0x80);
    return id;
}

std::string Uuid::toString() const {
    char buffer[37];
    std::snprintf(buffer, sizeof(buffer),
                  "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                  bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
                  bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
    return std::string(buffer);
}

static int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::optional<Uuid> Uuid::parse(const std::string& text) {
    // Canonical layout only, 8-4-4-4-12, as Foundation's UUID(uuidString:) demands.
    if (text.size() != 36) return std::nullopt;
    if (text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-') return std::nullopt;
    Uuid id;
    int out = 0;
    for (int i = 0; i < 36;) {
        if (text[i] == '-') { ++i; continue; }
        int hi = hexValue(text[i]);
        int lo = hexValue(text[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        id.bytes[out++] = static_cast<uint8_t>((hi << 4) | lo);
        i += 2;
    }
    return id;
}

}  // namespace compositor
