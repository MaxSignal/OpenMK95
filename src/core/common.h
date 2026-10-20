// Common helpers for the RM95 runtime reimplementation.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// All game texts are Shift_JIS byte strings; std::string is used as a byte container.
typedef std::string sjis;

inline int16_t s16(int32_t v) { return (int16_t)(uint16_t)(uint32_t)v; }
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline int32_t rds32(const uint8_t* p) { return (int32_t)rd32(p); }
inline int16_t rds16(const uint8_t* p) { return (int16_t)rd16(p); }
inline void wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
inline void wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// Byte buffer writer used by the save file code.
struct ByteWriter {
    std::vector<uint8_t> buf;
    void u8(uint8_t v) { buf.push_back(v); }
    void u16(uint16_t v) { u8((uint8_t)v); u8((uint8_t)(v >> 8)); }
    void u32(uint32_t v) { u16((uint16_t)v); u16((uint16_t)(v >> 16)); }
    void bytes(const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; buf.insert(buf.end(), b, b + n); }
    void zeros(size_t n) { buf.insert(buf.end(), n, 0); }
};

// Sequential reader with bounds tracking.
struct ByteReader {
    const uint8_t* p = nullptr;
    size_t n = 0, pos = 0;
    bool fail = false;
    ByteReader() {}
    ByteReader(const std::vector<uint8_t>& v) : p(v.data()), n(v.size()) {}
    ByteReader(const uint8_t* d, size_t len) : p(d), n(len) {}
    bool have(size_t k) const { return pos + k <= n; }
    size_t left() const { return pos < n ? n - pos : 0; }
    uint8_t u8() { if (!have(1)) { fail = true; pos = n; return 0; } return p[pos++]; }
    uint16_t u16() { if (!have(2)) { fail = true; pos = n; return 0; } uint16_t v = rd16(p + pos); pos += 2; return v; }
    uint32_t u32() { if (!have(4)) { fail = true; pos = n; return 0; } uint32_t v = rd32(p + pos); pos += 4; return v; }
    int32_t s32() { return (int32_t)u32(); }
    int16_t s16v() { return (int16_t)u16(); }
    bool bytes(void* dst, size_t k) {
        if (!have(k)) { size_t m = left(); if (m) memcpy(dst, p + pos, m); memset((uint8_t*)dst + m, 0, k - m); pos = n; fail = true; return false; }
        memcpy(dst, p + pos, k); pos += k; return true;
    }
    void skip(size_t k) { if (!have(k)) { fail = true; pos = n; } else pos += k; }
};

// C-like decimal conversion of a leading number (atoi semantics with optional sign).
inline int c_atoi(const char* s, size_t maxlen = (size_t)-1) {
    size_t i = 0;
    while (i < maxlen && s[i] && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == '\v' || s[i] == '\f')) i++;
    int sign = 1;
    if (i < maxlen && (s[i] == '+' || s[i] == '-')) { if (s[i] == '-') sign = -1; i++; }
    long long v = 0;
    while (i < maxlen && s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); if (v > 0x7fffffffLL) v = 0x7fffffffLL; i++; }
    return (int)(sign * v);
}

std::string fmt(const char* f, ...);
