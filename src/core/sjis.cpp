#include "core/sjis.h"
#include <cstdarg>
#include <cstdio>
#include <unordered_map>

#include "core/gen/cp932_table.inc"

std::string fmt(const char* f, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

namespace sj {

static int leadIndex(uint8_t lead) {
    if (lead >= 0x81 && lead <= 0x9F) return lead - 0x81;
    if (lead >= 0xE0 && lead <= 0xFC) return lead - 0xE0 + 31;
    return -1;
}

int charCount(const sjis& s) {
    int n = 0;
    for (size_t i = 0; i < s.size();) {
        if (isLead((uint8_t)s[i]) && i + 1 < s.size()) i += 2; else i += 1;
        n++;
    }
    return n;
}

int next(const sjis& s, size_t& i) {
    uint8_t b = (uint8_t)s[i];
    if (isLead(b) && i + 1 < s.size()) {
        int c = (b << 8) | (uint8_t)s[i + 1];
        i += 2;
        return c;
    }
    i += 1;
    return b;
}

static void putUtf8(std::string& o, unsigned u) {
    if (u < 0x80) o += (char)u;
    else if (u < 0x800) { o += (char)(0xC0 | (u >> 6)); o += (char)(0x80 | (u & 63)); }
    else { o += (char)(0xE0 | (u >> 12)); o += (char)(0x80 | ((u >> 6) & 63)); o += (char)(0x80 | (u & 63)); }
}

std::string toUtf8(const sjis& s) {
    std::string o;
    for (size_t i = 0; i < s.size();) {
        uint8_t b = (uint8_t)s[i];
        int li = leadIndex(b);
        if (li >= 0 && i + 1 < s.size()) {
            uint8_t t = (uint8_t)s[i + 1];
            unsigned u = (t >= 0x40 && t <= 0xFC) ? kCp932Double[li][t - 0x40] : 0;
            putUtf8(o, u ? u : 0xFFFD);
            i += 2;
        } else {
            unsigned u = kCp932Single[b];
            putUtf8(o, u ? u : 0xFFFD);
            i++;
        }
    }
    return o;
}

static std::unordered_map<unsigned, uint16_t>* g_rev = nullptr;

static void buildReverse() {
    if (g_rev) return;
    g_rev = new std::unordered_map<unsigned, uint16_t>();
    for (int b = 0; b < 256; b++)
        if (kCp932Single[b] && leadIndex((uint8_t)b) < 0) (*g_rev)[kCp932Single[b]] = (uint16_t)b;
    for (int li = 0; li < 60; li++) {
        int lead = li < 31 ? 0x81 + li : 0xE0 + li - 31;
        for (int t = 0x40; t <= 0xFC; t++) {
            unsigned u = kCp932Double[li][t - 0x40];
            if (u && !g_rev->count(u)) (*g_rev)[u] = (uint16_t)((lead << 8) | t);
        }
    }
}

bool fromUtf8(const std::string& u, sjis& out) {
    buildReverse();
    out.clear();
    bool ok = true;
    for (size_t i = 0; i < u.size();) {
        unsigned c = (uint8_t)u[i];
        int len = 1;
        if (c >= 0xF0) { len = 4; c &= 7; }
        else if (c >= 0xE0) { len = 3; c &= 15; }
        else if (c >= 0xC0) { len = 2; c &= 31; }
        else if (c >= 0x80) { ok = false; i++; continue; }
        if (i + len > u.size()) { ok = false; break; }
        for (int k = 1; k < len; k++) c = (c << 6) | ((uint8_t)u[i + k] & 63);
        i += len;
        auto it = g_rev->find(c);
        if (it == g_rev->end()) { ok = false; out += '?'; continue; }
        uint16_t v = it->second;
        if (v > 0xFF) { out += (char)(v >> 8); out += (char)(v & 0xFF); }
        else out += (char)v;
    }
    return ok;
}

sjis S(const char* utf8) {
    sjis o;
    fromUtf8(utf8, o);
    return o;
}

sjis foldName(const sjis& s) {
    sjis o = s;
    for (size_t i = 0; i < o.size();) {
        uint8_t b = (uint8_t)o[i];
        if (isLead(b) && i + 1 < o.size()) { i += 2; continue; }
        if (b >= 'a' && b <= 'z') o[i] = (char)(b - 32);
        i++;
    }
    return o;
}

// File-name lookup keys (02-data-formats.md §2.2). A Shift_JIS name and a host name
// stored in UTF-8 give the same key when they denote the same characters: ASCII
// letters are upper-cased, every other character is written as UTF-8 of its Unicode
// value. Bytes without a CP932 mapping are kept raw behind a 0xF8 marker (never part
// of valid UTF-8), so different unmapped names stay different. Trail bytes are part
// of their double-byte character and are never case-folded.
static void keyAscii(std::string& o, uint8_t b) { o += (char)((b >= 'a' && b <= 'z') ? b - 32 : b); }

std::string nameKey(const sjis& s) {
    std::string o;
    for (size_t i = 0; i < s.size();) {
        uint8_t b = (uint8_t)s[i];
        if (b < 0x80) { keyAscii(o, b); i++; continue; }
        int li = leadIndex(b);
        if (li >= 0 && i + 1 < s.size()) {
            uint8_t t = (uint8_t)s[i + 1];
            unsigned u = (t >= 0x40 && t <= 0xFC) ? kCp932Double[li][t - 0x40] : 0;
            if (u) putUtf8(o, u);
            else { o += (char)0xF8; o += (char)b; o += (char)t; }
            i += 2;
            continue;
        }
        unsigned u = kCp932Single[b];
        if (u && li < 0) putUtf8(o, u);
        else { o += (char)0xF8; o += (char)b; }
        i++;
    }
    return o;
}

std::string nameKeyUtf8(const std::string& u) {
    std::string o;
    for (char c : u) keyAscii(o, (uint8_t)c);
    return o;
}

sjis baseName(const sjis& s) {
    size_t p = s.rfind('\\');
    if (p == sjis::npos) return s;
    return s.substr(p + 1);
}

}  // namespace sj
