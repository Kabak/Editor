#include "encoding_qt.h"
#include "encoding.h"
#ifdef EDITOR_HAVE_TEXTCODEC
#include <QString>
#include <QTextCodec>

namespace {
void addQtCodec(const char *id, const char *group) {
    QTextCodec *codec = QTextCodec::codecForName(id);
    if (!codec) return;                              // not available in this Qt build
    const QByteArray name = codec->name();
    textenc::Codec entry;
    entry.info = {id, group};
    entry.decode = [name](const char *data, size_t size, std::u16string *out, std::string *error) {
        QTextCodec *c = QTextCodec::codecForName(name);
        QTextCodec::ConverterState state;
        const QString text = c->toUnicode(data, int(size), &state);
        if (state.invalidChars > 0 || state.remainingChars > 0) {
            *error = "the bytes are not valid for this encoding";
            return false;
        }
        out->assign(reinterpret_cast<const char16_t *>(text.utf16()), size_t(text.size()));
        return true;
    };
    entry.encode = [name](const std::u16string &text, bool lossy, std::string *out, size_t *bad) {
        QTextCodec *c = QTextCodec::codecForName(name);
        const QString whole = QString::fromUtf16(text.data(), int(text.size()));
        QTextCodec::ConverterState state;
        const QByteArray bytes = c->fromUnicode(whole.constData(), whole.size(), &state);
        if (state.invalidChars > 0 && !lossy) {      // find the first character that does not fit
            for (size_t i = 0; i < text.size(); ++i) {
                QTextCodec::ConverterState one;
                const QString piece = QString::fromUtf16(text.data() + i, 1);
                c->fromUnicode(piece.constData(), piece.size(), &one);
                if (one.invalidChars > 0) { *bad = i; return false; }
            }
            *bad = 0;
            return false;
        }
        out->assign(bytes.constData(), size_t(bytes.size()));
        return true;
    };
    textenc::registerCodec(std::move(entry));
}
}  // namespace

void registerQtCodecs() {
    static bool done = false;
    if (done) return;
    done = true;
    addQtCodec("GB18030", "East Asian");
    addQtCodec("GBK", "East Asian");
    addQtCodec("Big5", "East Asian");
    addQtCodec("Big5-HKSCS", "East Asian");
    addQtCodec("Shift_JIS", "East Asian");
    addQtCodec("EUC-JP", "East Asian");
    addQtCodec("ISO-2022-JP", "East Asian");
    addQtCodec("EUC-KR", "East Asian");
}
#else
void registerQtCodecs() {}
#endif
