// Shift_JIS (code page 932) helpers.
#pragma once
#include <string>
#include "core/common.h"

namespace sj {

inline bool isLead(uint8_t b) { return (b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC); }
// Number of characters (a lead+trail pair counts 1, every other byte counts 1).
int charCount(const sjis& s);
// UTF-8 conversion (for host file names and message boxes).
std::string toUtf8(const sjis& s);
// Convert UTF-8 to CP932. Returns false if some character has no CP932 mapping.
bool fromUtf8(const std::string& u, sjis& out);
// Literal helper: write program texts in UTF-8 source and convert once.
sjis S(const char* utf8);
// Windows-style case folding of a file name for lookups (ASCII letters only,
// trail bytes of double-byte characters untouched).
sjis foldName(const sjis& s);
// Case-insensitive lookup key of a Shift_JIS file name, and of a host name in UTF-8;
// both give the same key for the same characters (see sjis.cpp).
std::string nameKey(const sjis& s);
std::string nameKeyUtf8(const std::string& u);
// Base-name reduction of 02-data-formats.md §2.4 (byte-wise search for 0x5C).
sjis baseName(const sjis& s);
// Decode one character at s[i]: returns code (single byte or (lead<<8)|trail)
// and advances i.
int next(const sjis& s, size_t& i);

}  // namespace sj
