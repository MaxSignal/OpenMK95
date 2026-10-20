// Game folder access with Windows-like, Shift_JIS aware, case-insensitive lookup.
#pragma once
#include <string>
#include <vector>
#include "core/common.h"

namespace fsys {

// Set the game folder (host path, UTF-8 on Windows/macOS). Builds the index.
bool setGameDir(const std::string& hostPath);
const std::string& gameDirHost();
// Name of the game folder (last path component) as UTF-8 (window caption).
std::string gameDirNameUtf8();
// Same as Shift_JIS.
sjis gameDirNameSjis();
// Rescan the folder (after writing files).
void rescan();

// Lookups of a Shift_JIS file name directly in the game folder.
bool exists(const sjis& name);
bool read(const sjis& name, std::vector<uint8_t>& out);
// Absolute path (Shift_JIS, e.g. "C:\\...\\MCHIP0.ATR") used as it is.
bool readAbsolute(const sjis& path, std::vector<uint8_t>& out);
// Write a file into the game folder (creating or replacing it).
bool write(const sjis& name, const std::vector<uint8_t>& data);
// All names (Shift_JIS) in folder enumeration order whose folded name ends with ext
// (ext given in upper case, e.g. ".RPG").
std::vector<sjis> listWithExt(const char* extUpper);
// Host path (for diagnostics), UTF-8.
std::string displayPath(const sjis& name);

// Whole host file (outside the game folder), host path UTF-8.
bool readHost(const std::string& path, std::vector<uint8_t>& out);

}  // namespace fsys
