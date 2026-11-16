#include "encoding.h"
#include "encoding_tables.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <utility>
#include <unordered_map>

namespace textenc {
namespace {
enum class Kind { Utf8, Utf16LE, Utf16BE, Utf32LE, Utf32BE };
struct Unicode { const char *id; Kind kind; bool bom; };
const Unicode unicodeList[] = {
    {"UTF-8", Kind::Utf8, false}, {"UTF-8 BOM", Kind::Utf8, true},
    {"UTF-16 LE BOM", Kind::Utf16LE, true}, {"UTF-16 BE BOM", Kind::Utf16BE, true},
    {"UTF-16 LE", Kind::Utf16LE, false}, {"UTF-16 BE", Kind::Utf16BE, false},
    {"UTF-32 LE BOM", Kind::Utf32LE, true}, {"UTF-32 BE BOM", Kind::Utf32BE, true},
};
const Unicode *unicodeFor(const std::string &id) {
    for (const Unicode &u : unicodeList) if (id == u.id) return &u;
    return nullptr;
}
struct Single { const SingleByteTable *table; std::unordered_map<char16_t, unsigned char> reverse; };
struct Registry {
    std::vector<Info> infos;
    std::vector<Single> singles;                  // index matches singleIds
    std::vector<std::string> singleIds;
    std::vector<Codec> custom;
    Registry() {
        for (const Unicode &u : unicodeList) infos.push_back({u.id, "Unicode"});
        int count = 0;
        const SingleByteTable *tables = singleByteTables(&count);
        for (int i = 0; i < count; ++i) {
            Single s{&tables[i], {}};
            for (int b = 0; b < 128; ++b) s.reverse.emplace(tables[i].high[b], static_cast<unsigned char>(0x80 + b));
            singles.push_back(std::move(s));
            singleIds.push_back(tables[i].id);
            infos.push_back({tables[i].id, tables[i].group});
        }
    }
};
Registry &registry() { static Registry r; return r; }
const Single *singleFor(const std::string &id) {
    Registry &r = registry();
    for (size_t i = 0; i < r.singleIds.size(); ++i) if (r.singleIds[i] == id) return &r.singles[i];
    return nullptr;
}
const Codec *customFor(const std::string &id) {
    for (const Codec &c : registry().custom) if (c.info.id == id) return &c;
    return nullptr;
}
std::string offsetText(size_t offset) {
    // Avoids std::to_string, which some MinGW toolchains lack.
    std::string digits;
    do { digits.insert(digits.begin(), char('0' + offset % 10)); offset /= 10; } while (offset);
    return "at byte offset " + digits;
}

// ---- UTF-8 ----------------------------------------------------------------
// Returns the length of the valid sequence at p (1-4) or 0 if it is malformed.
int utf8Length(const unsigned char *p, size_t left) {
    const unsigned char b = p[0];
    if (b < 0x80) return 1;
    auto cont = [&](size_t i) { return i < left && (p[i] & 0xC0) == 0x80; };
    if (b >= 0xC2 && b <= 0xDF) return cont(1) ? 2 : 0;
    if (b >= 0xE0 && b <= 0xEF) {
        if (!cont(1) || !cont(2)) return 0;
        if (b == 0xE0 && p[1] < 0xA0) return 0;
        if (b == 0xED && p[1] > 0x9F) return 0;
        return 3;
    }
    if (b >= 0xF0 && b <= 0xF4) {
        if (!cont(1) || !cont(2) || !cont(3)) return 0;
        if (b == 0xF0 && p[1] < 0x90) return 0;
        if (b == 0xF4 && p[1] > 0x8F) return 0;
        return 4;
    }
    return 0;
}
void putCodePoint(std::u16string *out, uint32_t cp) {
    if (cp >= 0x10000) {
        cp -= 0x10000;
        out->push_back(char16_t(0xD800 + (cp >> 10)));
        out->push_back(char16_t(0xDC00 + (cp & 0x3FF)));
    } else out->push_back(char16_t(cp));
}
bool decodeUtf8(const char *data, size_t size, std::u16string *out, std::string *error) {
    const auto *p = reinterpret_cast<const unsigned char *>(data);
    out->clear();
    out->reserve(size);
    for (size_t i = 0; i < size;) {
        const int len = utf8Length(p + i, size - i);
        if (!len) { *error = "invalid UTF-8 sequence " + offsetText(i); return false; }
        uint32_t cp = len == 1 ? p[i] : len == 2 ? (p[i] & 0x1F) : len == 3 ? (p[i] & 0x0F) : (p[i] & 0x07);
        for (int k = 1; k < len; ++k) cp = (cp << 6) | (p[i + k] & 0x3F);
        putCodePoint(out, cp);
        i += len;
    }
    return true;
}
bool encodeUtf8(const std::u16string &text, bool lossy, std::string *out, size_t *bad) {
    out->clear();
    out->reserve(text.size() + text.size() / 2);
    auto put = [&](uint32_t cp) {
        if (cp < 0x80) out->push_back(char(cp));
        else if (cp < 0x800) { out->push_back(char(0xC0 | (cp >> 6))); out->push_back(char(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { out->push_back(char(0xE0 | (cp >> 12))); out->push_back(char(0x80 | ((cp >> 6) & 0x3F))); out->push_back(char(0x80 | (cp & 0x3F))); }
        else { out->push_back(char(0xF0 | (cp >> 18))); out->push_back(char(0x80 | ((cp >> 12) & 0x3F))); out->push_back(char(0x80 | ((cp >> 6) & 0x3F))); out->push_back(char(0x80 | (cp & 0x3F))); }
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const char16_t c = text[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            put(0x10000 + ((uint32_t(c) - 0xD800) << 10) + (uint32_t(text[i + 1]) - 0xDC00));
            ++i;
        } else if (c >= 0xD800 && c <= 0xDFFF) {          // lone surrogate: not representable
            if (!lossy) { *bad = i; return false; }
            out->push_back('?');
        } else put(c);
    }
    return true;
}
// ---- UTF-16 / UTF-32 --------------------------------------------------------
bool decodeUtf16(const char *data, size_t size, bool little, std::u16string *out, std::string *error) {
    if (size % 2) { *error = "the file has an odd number of bytes, so it cannot be UTF-16"; return false; }
    const auto *p = reinterpret_cast<const unsigned char *>(data);
    out->clear();
    out->reserve(size / 2);
    for (size_t i = 0; i < size; i += 2)
        out->push_back(char16_t(little ? (p[i] | (p[i + 1] << 8)) : ((p[i] << 8) | p[i + 1])));
    return true;
}
bool decodeUtf32(const char *data, size_t size, bool little, std::u16string *out, std::string *error) {
    if (size % 4) { *error = "the file size is not a multiple of 4, so it cannot be UTF-32"; return false; }
    const auto *p = reinterpret_cast<const unsigned char *>(data);
    out->clear();
    out->reserve(size / 4);
    for (size_t i = 0; i < size; i += 4) {
        const uint32_t cp = little ? (uint32_t(p[i]) | uint32_t(p[i + 1]) << 8 | uint32_t(p[i + 2]) << 16 | uint32_t(p[i + 3]) << 24)
                                   : (uint32_t(p[i]) << 24 | uint32_t(p[i + 1]) << 16 | uint32_t(p[i + 2]) << 8 | uint32_t(p[i + 3]));
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { *error = "invalid UTF-32 code point " + offsetText(i); return false; }
        putCodePoint(out, cp);
    }
    return true;
}
void put16(std::string *out, char16_t c, bool little) {
    if (little) { out->push_back(char(c & 0xFF)); out->push_back(char(c >> 8)); }
    else { out->push_back(char(c >> 8)); out->push_back(char(c & 0xFF)); }
}
bool encodeUtf32(const std::u16string &text, bool little, bool lossy, std::string *out, size_t *bad) {
    out->clear();
    out->reserve(text.size() * 4);
    auto put = [&](uint32_t cp) {
        const char b[4] = {char(cp & 0xFF), char((cp >> 8) & 0xFF), char((cp >> 16) & 0xFF), char(cp >> 24)};
        if (little) out->append(b, 4);
        else for (int k = 3; k >= 0; --k) out->push_back(b[k]);
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const char16_t c = text[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            put(0x10000 + ((uint32_t(c) - 0xD800) << 10) + (uint32_t(text[i + 1]) - 0xDC00));
            ++i;
        } else if (c >= 0xD800 && c <= 0xDFFF) {
            if (!lossy) { *bad = i; return false; }
            put('?');
        } else put(c);
    }
    return true;
}
bool hasBom(const std::string &bom, const char *data, size_t size) { return size >= bom.size() && std::memcmp(data, bom.data(), bom.size()) == 0; }
std::string bomFor(Kind kind) {
    switch (kind) {
    case Kind::Utf8: return "\xEF\xBB\xBF";
    case Kind::Utf16LE: return "\xFF\xFE";
    case Kind::Utf16BE: return "\xFE\xFF";
    case Kind::Utf32LE: return std::string("\xFF\xFE\0\0", 4);
    case Kind::Utf32BE: return std::string("\0\0\xFE\xFF", 4);
    }
    return {};
}
// ---- detection scoring ------------------------------------------------------
enum Cls { Other = 0, Lower = 1, Upper = 2, AsciiLetter = 3, HighLower = 4, HighUpper = 5 };
bool cyrLower(char16_t c) { return (c >= 0x0430 && c <= 0x044F) || c == 0x0451 || c == 0x0454 || c == 0x0455 || c == 0x0456 || c == 0x0457 || c == 0x0491; }
bool cyrUpper(char16_t c) { return (c >= 0x0410 && c <= 0x042F) || c == 0x0401 || c == 0x0404 || c == 0x0405 || c == 0x0406 || c == 0x0407 || c == 0x0490; }
// Latin Extended-A alternates upper/lower case, with the parity flipped in two blocks.
bool latExtUpper(char16_t c) {
    if (c < 0x0100 || c > 0x017F) return false;
    const bool flipped = (c >= 0x0139 && c <= 0x0148) || (c >= 0x0179 && c <= 0x017E);
    return flipped ? (c & 1) != 0 : (c & 1) == 0;
}
bool latLower(char16_t c) { return (c >= 0x00DF && c <= 0x00FF && c != 0x00F7) || (c >= 0x0100 && c <= 0x017F && !latExtUpper(c)); }
bool latUpper(char16_t c) { return (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7) || latExtUpper(c); }

// Scores how natural the bytes look when read with the given table. Words that
// switch case in the middle, box-drawing/symbol characters and C1 controls are
// what a wrong Cyrillic code page produces; long runs of accented Latin letters
// are what Cyrillic text read as Western looks like.
double score(const SingleByteTable &t, const unsigned char *p, size_t n, bool latin) {
    double total = 0;
    int prev = Other, run = 0;                       // run: consecutive non-ASCII letters
    for (size_t i = 0; i < n; ++i) {
        const unsigned char b = p[i];
        if (b < 0x80) {
            const bool letter = (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z');
            if (letter && !latin && (prev == Lower || prev == Upper)) total -= 2;   // Latin glued to a Cyrillic word
            prev = letter ? AsciiLetter : Other;
            run = 0;
            continue;
        }
        const char16_t c = t.high[b - 0x80];
        if (c < 0x00A0 && c == b) { total -= 4; prev = Other; run = 0; continue; }  // undefined / C1 control
        const bool lower = latin ? latLower(c) : cyrLower(c), upper = latin ? latUpper(c) : cyrUpper(c);
        if (!lower && !upper) { total -= 3; prev = Other; run = 0; continue; }
        ++run;
        if (latin) {
            if (run >= 3) total -= 3;                                // Cyrillic read as Western looks like this
            else if (lower) total += prev == AsciiLetter ? 2 : 1;
            else total += prev == HighLower || prev == AsciiLetter ? -2 : 1;
            prev = lower ? HighLower : HighUpper;
        } else {
            const bool inWord = prev == Lower || prev == Upper;
            if (prev == AsciiLetter) total -= 2;                     // Cyrillic glued to a Latin word
            else if (lower) total += inWord ? 2 : 1;
            else total += prev == Lower ? -4 : 1;                    // capital in the middle of a word
            prev = lower ? Lower : Upper;
        }
    }
    return total;
}
}  // namespace

const std::vector<Info> &encodings() { return registry().infos; }
bool isKnown(const std::string &id) { return unicodeFor(id) || singleFor(id) || customFor(id); }
void registerCodec(Codec codec) {
    Registry &r = registry();
    for (Codec &c : r.custom) if (c.info.id == codec.info.id) { c = std::move(codec); return; }
    r.infos.push_back(codec.info);
    r.custom.push_back(std::move(codec));
}
bool isValidUtf8(const char *data, size_t size) {
    const auto *p = reinterpret_cast<const unsigned char *>(data);
    for (size_t i = 0; i < size;) {
        if (p[i] < 0x80) { ++i; continue; }
        const int len = utf8Length(p + i, size - i);
        if (!len) return false;
        i += len;
    }
    return true;
}
bool decode(const std::string &id, const char *data, size_t size, std::u16string *out, std::string *error) {
    if (const Unicode *u = unicodeFor(id)) {
        if (u->bom) {
            const std::string bom = bomFor(u->kind);
            if (hasBom(bom, data, size)) { data += bom.size(); size -= bom.size(); }
        }
        switch (u->kind) {
        case Kind::Utf8: return decodeUtf8(data, size, out, error);
        case Kind::Utf16LE: return decodeUtf16(data, size, true, out, error);
        case Kind::Utf16BE: return decodeUtf16(data, size, false, out, error);
        case Kind::Utf32LE: return decodeUtf32(data, size, true, out, error);
        case Kind::Utf32BE: return decodeUtf32(data, size, false, out, error);
        }
    }
    if (const Single *s = singleFor(id)) {
        out->resize(size);
        const auto *p = reinterpret_cast<const unsigned char *>(data);
        for (size_t i = 0; i < size; ++i) (*out)[i] = p[i] < 0x80 ? char16_t(p[i]) : s->table->high[p[i] - 0x80];
        return true;
    }
    if (const Codec *c = customFor(id)) return c->decode(data, size, out, error);
    *error = "unknown encoding: " + id;
    return false;
}
bool encode(const std::string &id, const std::u16string &text, bool lossy, std::string *out, size_t *bad) {
    if (const Unicode *u = unicodeFor(id)) {
        std::string body;
        bool ok = true;
        switch (u->kind) {
        case Kind::Utf8: ok = encodeUtf8(text, lossy, &body, bad); break;
        case Kind::Utf16LE: case Kind::Utf16BE:
            body.reserve(text.size() * 2);
            for (char16_t c : text) put16(&body, c, u->kind == Kind::Utf16LE);
            break;
        case Kind::Utf32LE: case Kind::Utf32BE: ok = encodeUtf32(text, u->kind == Kind::Utf32LE, lossy, &body, bad); break;
        }
        if (!ok) return false;
        *out = u->bom ? bomFor(u->kind) + body : std::move(body);
        return true;
    }
    if (const Single *s = singleFor(id)) {
        out->clear();
        out->reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            const char16_t c = text[i];
            if (c < 0x80) { out->push_back(char(c)); continue; }
            auto it = s->reverse.find(c);
            if (it != s->reverse.end()) { out->push_back(char(it->second)); continue; }
            if (!lossy) { *bad = i; return false; }
            out->push_back('?');
            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) ++i;
        }
        return true;
    }
    if (const Codec *c = customFor(id)) return c->encode(text, lossy, out, bad);
    *bad = 0;
    return false;
}

Detection detect(const char *data, size_t size, bool cyrillicDefault) {
    Detection result;
    const auto *p = reinterpret_cast<const unsigned char *>(data);
    // 1. Byte order marks (UTF-32 first: its LE mark starts with the UTF-16 LE mark).
    if (hasBom(bomFor(Kind::Utf32LE), data, size)) return {"UTF-32 LE BOM", false};
    if (hasBom(bomFor(Kind::Utf32BE), data, size)) return {"UTF-32 BE BOM", false};
    if (hasBom(bomFor(Kind::Utf8), data, size)) return {"UTF-8 BOM", false};
    if (hasBom(bomFor(Kind::Utf16LE), data, size)) return {"UTF-16 LE BOM", false};
    if (hasBom(bomFor(Kind::Utf16BE), data, size)) return {"UTF-16 BE BOM", false};
    size_t nul = 0, high = 0, oddNul = 0, evenNul = 0, odd04 = 0, even04 = 0;
    for (size_t i = 0; i < size; ++i) {
        if (p[i] == 0) { ++nul; ++(i & 1 ? oddNul : evenNul); }
        else if (p[i] >= 0x80) ++high;
        if (p[i] == 0x04) ++(i & 1 ? odd04 : even04);
    }
    // 2. UTF-16 without a BOM: ASCII text has a NUL in every other byte, Cyrillic has 0x04.
    if (size >= 4 && size % 2 == 0) {
        const size_t half = size / 2;
        // The high byte of ASCII is 0x00 and of Cyrillic is 0x04; the low byte is almost never 0x00.
        if (evenNul * 20 <= half && (oddNul + odd04) * 10 >= half * 5) return {"UTF-16 LE", false};
        if (oddNul * 20 <= half && (evenNul + even04) * 10 >= half * 5) return {"UTF-16 BE", false};
    }
    if (nul) { result.binary = true; return result; }
    // 3. Plain ASCII and valid UTF-8 are by far the most likely.
    if (high == 0 || isValidUtf8(data, size)) return {"UTF-8", false};
    // 4. A legacy 8-bit code page: pick the one that reads most naturally.
    struct Candidate { const char *id; bool latin; };
    static const Candidate cyr[] = {{"Windows-1251", false}, {"KOI8-R", false}, {"CP866 (DOS)", false}, {"MacCyrillic", false}, {"ISO-8859-5", false}};
    static const Candidate lat[] = {{"Windows-1252", true}, {"Windows-1250", true}};
    std::vector<Candidate> order;
    if (cyrillicDefault) { order.assign(std::begin(cyr), std::end(cyr)); order.insert(order.end(), std::begin(lat), std::end(lat)); }
    else { order.assign(std::begin(lat), std::end(lat)); order.insert(order.end(), std::begin(cyr), std::end(cyr)); }
    const char *best = cyrillicDefault ? "Windows-1251" : "Windows-1252";
    double bestScore = 0.5 * double(high);           // must beat this to override the default
    for (const Candidate &c : order) {
        const Single *s = singleFor(c.id);
        if (!s) continue;
        const double value = score(*s->table, p, size, c.latin);
        if (value > bestScore) { bestScore = value; best = c.id; }
    }
    return {best, false};
}
}  // namespace textenc
