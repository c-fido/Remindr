#pragma once

#include <cstdio>
#include <random>
#include <sstream>
#include <string>

inline std::string uuid_v4() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    auto r1 = dist(rng);
    auto r2 = dist(rng);
    auto r3 = dist(rng);
    auto r4 = dist(rng);

    // RFC 4122 version 4
    r2 = (r2 & 0xFFFF0FFFU) | 0x00004000U;
    r3 = (r3 & 0x3FFFFFFFU) | 0x80000000U;

    char buf[37];
    std::snprintf(buf, sizeof(buf),
        "%08x-%04x-%04x-%04x-%012llx",
        r1,
        (r2 >> 16) & 0xFFFFU,
        r2 & 0xFFFFU,
        (r3 >> 16) & 0xFFFFU,
        (static_cast<unsigned long long>(r3) << 32) | r4);

    return buf;
}
