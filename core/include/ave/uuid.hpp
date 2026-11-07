#pragma once

#include <string>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstdint>

namespace ave {

class UUID {
public:
    static std::string generate() {
        // RFC 4122 version 4 UUID generator
        thread_local std::random_device rd;
        thread_local std::mt19937_64 gen(rd());
        thread_local std::uniform_int_distribution<uint64_t> dist;

        uint64_t high = dist(gen);
        uint64_t low = dist(gen);

        // Set version 4 (bits 12-15 of time_hi_and_version to 0100)
        high = (high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        // Set variant 1 (bits 6-7 of clock_seq_hi_and_reserved to 10)
        low = (low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

        std::ostringstream ss;
        ss << std::hex << std::setfill('0')
           << std::setw(8) << (high >> 32)
           << '-'
           << std::setw(4) << ((high >> 16) & 0xFFFF)
           << '-'
           << std::setw(4) << (high & 0xFFFF)
           << '-'
           << std::setw(4) << (low >> 48)
           << '-'
           << std::setw(12) << (low & 0xFFFFFFFFFFFFULL);

        return ss.str();
    }

    static bool is_valid(const std::string& str) {
        if (str.length() != 36) return false;
        if (str[8] != '-' || str[13] != '-' || str[18] != '-' || str[23] != '-') return false;
        if (str[14] != '4') return false; // version 4
        for (size_t i = 0; i < str.length(); ++i) {
            if (i == 8 || i == 13 || i == 18 || i == 23) continue;
            char c = str[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                return false;
            }
        }
        return true;
    }
};

} // namespace ave
