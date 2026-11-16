#pragma once
// Text-encoding support that does not depend on Qt: detection, strict decoding
// to UTF-16 and encoding back. Everything is table driven, so the same code
// behaves identically on Windows, Ubuntu and Linux Mint with any Qt version.
//
// Adding an encoding:
//   * single-byte code page: add one line to tools/gen_tables.py, re-run it
//     (python3 tools/gen_tables.py > src/encoding_tables.h) and rebuild;
//   * anything else: call textenc::registerCodec() once at start-up (see
//     encoding_qt.cpp, which does this for every codec Qt knows about).
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace textenc {
struct Info { std::string id, group; };

// Decoder: bytes -> UTF-16. Return false and set *error if the bytes are not valid.
using DecodeFn = std::function<bool(const char *, size_t, std::u16string *, std::string *)>;
// Encoder: UTF-16 -> bytes. Characters that cannot be represented are reported
// through *bad (UTF-16 index of the first one) unless lossy is true, in which
// case each is written as '?'.
using EncodeFn = std::function<bool(const std::u16string &, bool lossy, std::string *, size_t *bad)>;
struct Codec { Info info; DecodeFn decode; EncodeFn encode; };

const std::vector<Info> &encodings();           // everything that can be chosen in the menus
bool isKnown(const std::string &id);
void registerCodec(Codec codec);                // replaces an existing codec with the same id

bool decode(const std::string &id, const char *data, size_t size, std::u16string *out, std::string *error);
bool encode(const std::string &id, const std::u16string &text, bool lossy, std::string *out, size_t *bad);

struct Detection { std::string id; bool binary = false; };
// Picks the most likely encoding of a file. Never fails for text: when the
// content is ambiguous it falls back to Windows-1251 (cyrillicDefault) or Windows-1252.
Detection detect(const char *data, size_t size, bool cyrillicDefault);
bool isValidUtf8(const char *data, size_t size);
}
