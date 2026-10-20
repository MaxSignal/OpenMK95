#include "core/fsys.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include "core/sjis.h"

namespace stdfs = std::filesystem;

namespace fsys {

struct Entry {
    sjis name;          // Shift_JIS name
    stdfs::path path;   // host path
    std::string key;    // lookup key (sj::nameKey / sj::nameKeyUtf8)
    std::string altKey; // second key when the host name has two readings (raw SJIS / UTF-8)
};

static std::string g_dir;
static stdfs::path g_dirPath;
static std::vector<Entry> g_entries;            // sorted by Shift_JIS name (deterministic order)
static std::map<std::string, size_t> g_index;   // lookup key -> entry
static bool g_rawSjisNames = false;             // host names stored as raw SJIS bytes (Linux)

static bool validUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        uint8_t c = (uint8_t)s[i];
        int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!len || i + len > s.size()) return false;
        for (int k = 1; k < len; k++)
            if (((uint8_t)s[i + k] >> 6) != 2) return false;
        i += len;
    }
    return true;
}

static stdfs::path u8path(const std::string& s) {
#if defined(__cpp_char8_t)
    return stdfs::path(std::u8string(s.begin(), s.end()));
#else
    return stdfs::u8path(s);
#endif
}

static std::string pathU8(const stdfs::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

// Shift_JIS name and lookup keys of a host file name. Names in UTF-8 (Windows, and
// most Linux/macOS copies) are converted; names stored as raw Shift_JIS bytes (as in
// archives unpacked without conversion) are used as they are. A raw Shift_JIS name
// can happen to be valid UTF-8, so such names are indexed under both readings.
static void hostName(const stdfs::path& fname, Entry& e) {
#ifdef _WIN32
    std::string u = pathU8(fname);
    sj::fromUtf8(u, e.name);
    e.key = sj::nameKeyUtf8(u);
#else
    std::string raw = fname.native();
    bool ascii = true;
    for (char c : raw) if ((uint8_t)c >= 0x80) { ascii = false; break; }
    if (ascii) { e.name = raw; e.key = sj::nameKey(raw); return; }
    sjis conv;
    if (validUtf8(raw) && sj::fromUtf8(raw, conv)) {
        e.name = conv;
        e.key = sj::nameKeyUtf8(raw);
        e.altKey = sj::nameKey(raw);
        return;
    }
    g_rawSjisNames = true;
    e.name = raw;
    e.key = sj::nameKey(raw);
#endif
}

static sjis hostToSjis(const stdfs::path& fname) {
    bool raw = g_rawSjisNames;
    Entry e;
    hostName(fname, e);
    g_rawSjisNames = raw;  // only the names inside the game folder decide this
    return e.name;
}

void rescan() {
    g_entries.clear();
    g_index.clear();
    std::error_code ec;
    for (auto it = stdfs::directory_iterator(g_dirPath, ec); !ec && it != stdfs::directory_iterator(); it.increment(ec)) {
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        Entry e;
        e.path = it->path();
        hostName(it->path().filename(), e);
        g_entries.push_back(e);
    }
    // The original takes e.g. the last .rpg file of the directory enumeration, an order
    // defined by the file system (02 §2.1); sort for a deterministic choice.
    std::sort(g_entries.begin(), g_entries.end(), [](const Entry& a, const Entry& b) {
        return a.name != b.name ? a.name < b.name : a.path.native() < b.path.native();
    });
    // Primary keys first, then the alternative readings where they do not collide.
    for (size_t i = 0; i < g_entries.size(); i++) g_index[g_entries[i].key] = i;
    for (size_t i = 0; i < g_entries.size(); i++)
        if (!g_entries[i].altKey.empty()) g_index.emplace(g_entries[i].altKey, i);
}

bool setGameDir(const std::string& hostPath) {
    g_dir = hostPath;
    g_dirPath = u8path(hostPath);
    std::error_code ec;
    if (!stdfs::is_directory(g_dirPath, ec)) return false;
    rescan();
    return true;
}

const std::string& gameDirHost() { return g_dir; }

std::string gameDirNameUtf8() {
    stdfs::path p = g_dirPath;
    std::string name = pathU8(p.filename());
    if (name.empty() || name == ".") name = pathU8(stdfs::absolute(p).parent_path().filename());
    if (!validUtf8(name)) return sj::toUtf8(name);
    return name;
}

sjis gameDirNameSjis() {
    stdfs::path p = g_dirPath;
    if (p.filename().empty() || p.filename() == ".") p = stdfs::absolute(p).parent_path();
    return hostToSjis(p.filename());
}

static const Entry* find(const sjis& name) {
    auto it = g_index.find(sj::nameKey(name));
    if (it == g_index.end()) return nullptr;
    return &g_entries[it->second];
}

bool exists(const sjis& name) { return find(name) != nullptr; }

static bool readPath(const stdfs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    if (n < 0) return false;
    f.seekg(0);
    out.resize((size_t)n);
    if (n) f.read((char*)out.data(), n);
    return (bool)f || f.eof();
}

bool read(const sjis& name, std::vector<uint8_t>& out) {
    const Entry* e = find(name);
    if (!e) return false;
    return readPath(e->path, out);
}

bool readAbsolute(const sjis& path, std::vector<uint8_t>& out) {
#ifdef _WIN32
    return readPath(u8path(sj::toUtf8(path)), out);
#else
    (void)path; (void)out;
    return false;  // drive-letter paths cannot exist on this host
#endif
}

bool write(const sjis& name, const std::vector<uint8_t>& data) {
    stdfs::path p;
    if (const Entry* e = find(name)) p = e->path;
    else {
#ifdef _WIN32
        p = g_dirPath / u8path(sj::toUtf8(name));
#else
        p = g_dirPath / (g_rawSjisNames ? stdfs::path(name) : u8path(sj::toUtf8(name)));
#endif
    }
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!data.empty()) f.write((const char*)data.data(), (std::streamsize)data.size());
    f.close();
    bool ok = !f.fail();
    rescan();
    return ok;
}

std::vector<sjis> listWithExt(const char* extUpper) {
    std::vector<sjis> r;
    size_t el = strlen(extUpper);
    for (auto& e : g_entries) {
        const std::string& f = e.key;
        if (f.size() >= el && f.compare(f.size() - el, el, extUpper) == 0) r.push_back(e.name);
    }
    return r;
}

std::string displayPath(const sjis& name) {
    return g_dir + "/" + sj::toUtf8(name);
}

bool readHost(const std::string& path, std::vector<uint8_t>& out) { return readPath(u8path(path), out); }

}  // namespace fsys
