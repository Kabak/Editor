// Plain C++ tests for the Qt-independent encoding module (no Qt needed to run them).
#include "encoding.h"
#include <cstdio>
#include <string>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAILED line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

static std::u16string decodeOrDie(const std::string &id, const std::string &bytes) {
    std::u16string out;
    std::string error;
    CHECK(textenc::decode(id, bytes.data(), bytes.size(), &out, &error));
    return out;
}
int main() {
    // "Привет, мир! Это проверка кодировки." in the Cyrillic code pages.
    const std::u16string text = u"Привет, мир! Это проверка кодировки.\n";
    struct Case { const char *id; std::string bytes; };
    const Case cases[] = {
        {"Windows-1251", "\xCF\xF0\xE8\xE2\xE5\xF2, \xEC\xE8\xF0! \xDD\xF2\xEE \xEF\xF0\xEE\xE2\xE5\xF0\xEA\xE0 \xEA\xEE\xE4\xE8\xF0\xEE\xE2\xEA\xE8.\n"},
        {"CP866 (DOS)", "\x8F\xE0\xA8\xA2\xA5\xE2, \xAC\xA8\xE0! \x9D\xE2\xAE \xAF\xE0\xAE\xA2\xA5\xE0\xAA\xA0 \xAA\xAE\xA4\xA8\xE0\xAE\xA2\xAA\xA8.\n"},
        {"KOI8-R", "\xF0\xD2\xC9\xD7\xC5\xD4, \xCD\xC9\xD2! \xFC\xD4\xCF \xD0\xD2\xCF\xD7\xC5\xD2\xCB\xC1 \xCB\xCF\xC4\xC9\xD2\xCF\xD7\xCB\xC9.\n"},
    };
    for (const Case &c : cases) {
        CHECK(decodeOrDie(c.id, c.bytes) == text);
        std::string out;
        size_t bad = 0;
        CHECK(textenc::encode(c.id, text, false, &out, &bad) && out == c.bytes);
        for (bool cyrillicDefault : {true, false}) {
            const textenc::Detection d = textenc::detect(c.bytes.data(), c.bytes.size(), cyrillicDefault);
            if (d.id != c.id) std::printf("detected %s instead of %s\n", d.id.c_str(), c.id);
            CHECK(d.id == c.id && !d.binary);
        }
    }
    // UTF-8 (valid), UTF-8 with BOM, UTF-16 with BOM, binary data.
    const std::string utf8 = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
    CHECK(textenc::detect(utf8.data(), utf8.size(), false).id == "UTF-8");
    const std::string bom8 = "\xEF\xBB\xBF" + utf8;
    CHECK(textenc::detect(bom8.data(), bom8.size(), false).id == "UTF-8 BOM");
    CHECK(decodeOrDie("UTF-8 BOM", bom8) == decodeOrDie("UTF-8", utf8));
    const std::string u16 = std::string("\xFF\xFE", 2) + std::string("H\0i\0", 4);
    CHECK(textenc::detect(u16.data(), u16.size(), false).id == "UTF-16 LE BOM");
    CHECK(decodeOrDie("UTF-16 LE BOM", u16) == u"Hi");
    const std::string binary("\x01\x00\x02\x00\x00\x03", 6);
    CHECK(textenc::detect(binary.data(), binary.size(), false).binary);
    const std::string ascii = "plain ascii text\n";
    CHECK(textenc::detect(ascii.data(), ascii.size(), true).id == "UTF-8");
    // Strict UTF-8: a CP1251 byte sequence must be rejected, never silently altered.
    std::u16string tmp;
    std::string error;
    CHECK(!textenc::decode("UTF-8", cases[0].bytes.data(), cases[0].bytes.size(), &tmp, &error));
    // Characters missing from a code page are reported, or replaced only on request.
    std::string out;
    size_t bad = 99;
    CHECK(!textenc::encode("Windows-1251", u"a中b", false, &out, &bad) && bad == 1);
    CHECK(textenc::encode("Windows-1251", u"a中b", true, &out, &bad) && out == "a?b");
    // Every byte value survives decode -> encode in every single-byte encoding.
    std::string all;
    for (int i = 0; i < 256; ++i) all.push_back(char(i));
    for (const textenc::Info &info : textenc::encodings()) {
        if (info.group == "Unicode") continue;
        std::u16string wide;
        std::string back;
        CHECK(textenc::decode(info.id, all.data(), all.size(), &wide, &error));
        CHECK(textenc::encode(info.id, wide, false, &back, &bad) && back == all);
    }
    if (!failures) std::printf("encoding tests passed\n");
    return failures ? 1 : 0;
}
