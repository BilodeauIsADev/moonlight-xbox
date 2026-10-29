#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace moonlight_xbox_dx { namespace WakeOnLan {

inline constexpr int Ports[] = { 7, 9, 47009, 47998, 47999, 48000, 48002, 48010 };

inline bool ParseIPv4(const std::string& text, uint32_t& value) {
    value = 0;
    size_t pos = 0;
    for (int octet = 0; octet < 4; ++octet) {
        unsigned part = 0;
        int digits = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            part = part * 10 + (text[pos++] - '0');
            if (++digits > 3 || part > 255) return false;
        }
        if (!digits) return false;
        value = (value << 8) | part;
        if (octet < 3 && (pos == text.size() || text[pos++] != '.')) return false;
    }
    return pos == text.size();
}

inline std::string FormatIPv4(uint32_t value) {
    return std::to_string(value >> 24) + "." + std::to_string((value >> 16) & 255) + "." +
        std::to_string((value >> 8) & 255) + "." + std::to_string(value & 255);
}

inline std::string HostIPv4(const std::string& endpoint) {
    const auto address = endpoint.substr(0, endpoint.find_first_of(":/"));
    uint32_t value;
    return ParseIPv4(address, value) ? FormatIPv4(value) : std::string();
}

struct InterfaceIPv4 {
    std::string address;
    unsigned prefixLength;
};

inline std::string DirectedBroadcast(const std::string& host, const std::vector<InterfaceIPv4>& interfaces) {
    uint32_t target;
    if (!ParseIPv4(HostIPv4(host), target)) return {};
    unsigned bestPrefix = 0;
    std::string broadcast;
    for (const auto& adapter : interfaces) {
        // /31 and /32 interfaces have no directed broadcast. Unknown prefixes are not guessed.
        if (adapter.prefixLength == 0 || adapter.prefixLength > 30) continue;
        uint32_t local;
        if (!ParseIPv4(adapter.address, local)) continue;
        const uint32_t mask = UINT32_MAX << (32 - adapter.prefixLength);
        if ((target & mask) == (local & mask) && adapter.prefixLength > bestPrefix) {
            bestPrefix = adapter.prefixLength;
            broadcast = FormatIPv4(local | ~mask);
        }
    }
    return broadcast;
}

inline int HexDigit(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

inline bool ParseMac(const std::wstring& text, std::array<unsigned char, 6>& bytes) {
    const bool separated = text.size() == 17;
    if (!separated && text.size() != 12) return false;
    const wchar_t separator = separated ? text[2] : 0;
    if (separated && separator != L':' && separator != L'-') return false;
    bool nonzero = false;
    for (size_t i = 0; i < bytes.size(); ++i) {
        const auto pos = i * (separated ? 3 : 2);
        const int high = HexDigit(text[pos]), low = HexDigit(text[pos + 1]);
        if (high < 0 || low < 0 || (separated && i < 5 && text[pos + 2] != separator)) return false;
        bytes[i] = static_cast<unsigned char>((high << 4) | low);
        nonzero = nonzero || bytes[i] != 0;
    }
    return nonzero && (bytes[0] & 1) == 0;
}

inline bool IsUsableMac(const std::wstring& text) {
    std::array<unsigned char, 6> bytes{};
    return ParseMac(text, bytes);
}

inline std::string MagicPacket(const std::wstring& mac) {
    std::array<unsigned char, 6> bytes{};
    if (!ParseMac(mac, bytes)) throw std::invalid_argument("A valid unicast MAC address is required for Wake-on-LAN.");
    std::string packet(6, static_cast<char>(0xFF));
    for (int repeat = 0; repeat < 16; ++repeat)
        for (auto byte : bytes) packet.push_back(static_cast<char>(byte));
    return packet;
}

} }
