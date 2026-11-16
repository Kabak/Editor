#include "fileio.h"
#include "encoding.h"
#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSaveFile>
#include <limits>

QString TextFile::identity(const QString &path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}
bool TextFile::samePath(const QString &first, const QString &second) {
#ifdef Q_OS_WIN
    return identity(first).compare(identity(second), Qt::CaseInsensitive) == 0;
#else
    return identity(first) == identity(second);
#endif
}
bool TextFile::read(const QString &path, QString *text, TextFileFormat *format, QString *error, const QString &forcedEncoding) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) { *error = "The file does not exist or is not a regular file."; return false; }
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) { *error = input.errorString(); return false; }
    if (input.size() > std::numeric_limits<int>::max() / 2) {
        *error = "This file exceeds the editor's text-buffer capacity (approximately 1 GiB).";
        return false;
    }
    QByteArray bytes = input.readAll();
    if (input.error() != QFileDevice::NoError) { *error = input.errorString(); return false; }
    TextFileFormat candidate;
    candidate.digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    std::string id;
    if (forcedEncoding.isEmpty()) {
        const bool cyrillicLocale = QLocale::system().script() == QLocale::CyrillicScript;
        const textenc::Detection found = textenc::detect(bytes.constData(), size_t(bytes.size()), cyrillicLocale);
        if (found.binary) {
            *error = "This file looks like binary data (it contains NUL bytes), not text. "
                     "To force an encoding, use Encoding > Open with encoding...";
            return false;
        }
        id = found.id;
    } else id = forcedEncoding.toStdString();
    std::u16string wide;
    std::string reason;
    if (!textenc::decode(id, bytes.constData(), size_t(bytes.size()), &wide, &reason)) {
        *error = QString("The file cannot be read as %1: %2.").arg(QString::fromStdString(id), QString::fromStdString(reason));
        return false;
    }
    bytes.clear();
    bytes.squeeze();
    QString decoded = QString::fromUtf16(wide.data(), int(wide.size()));
    std::u16string().swap(wide);
    candidate.encoding = QByteArray::fromStdString(id);
    const int crlf = decoded.count(QStringLiteral("\r\n")), lf = decoded.count(QLatin1Char('\n')) - crlf, cr = decoded.count(QLatin1Char('\r')) - crlf;
    if (crlf > 0 && crlf >= lf && crlf >= cr) candidate.newline = "\r\n";
    else if (cr > lf) candidate.newline = "\r";
    decoded.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    *text = decoded;
    *format = candidate;
    return true;
}
bool TextFile::changedOnDisk(const QString &path, const QByteArray &digest) {
    if (digest.isEmpty()) return false;
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) return true;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&input)) return true;
    return hash.result() != digest;
}
bool TextFile::write(const QString &path, const QString &text, TextFileFormat *format, QString *error, bool lossy, bool *unmappable) {
    if (unmappable) *unmappable = false;
    QString body = text;
    if (format->newline != "\n") body.replace(QLatin1Char('\n'), QString::fromLatin1(format->newline));
    const std::u16string wide(reinterpret_cast<const char16_t *>(body.utf16()), size_t(body.size()));
    std::string encoded;
    size_t bad = 0;
    const std::string id = format->encoding.toStdString();
    if (!textenc::isKnown(id)) { *error = "Unknown encoding: " + format->encoding; return false; }
    if (!textenc::encode(id, wide, lossy, &encoded, &bad)) {
        if (unmappable) *unmappable = true;
        const QChar lineBreak = format->newline == "\r" ? QLatin1Char('\r') : QLatin1Char('\n');
        int line = 1, lineStart = 0;
        for (int i = 0; i < int(bad) && i < body.size(); ++i)
            if (body.at(i) == lineBreak) { ++line; lineStart = i + 1; }
        const int length = (int(bad) + 1 < body.size() && body.at(int(bad)).isHighSurrogate() && body.at(int(bad) + 1).isLowSurrogate()) ? 2 : 1;
        const QString character = body.mid(int(bad), length);
        const uint codePoint = length == 2 ? QChar::surrogateToUcs4(character[0], character[1]) : character.isEmpty() ? 0 : character[0].unicode();
        *error = QString("The character U+%1 \"%2\" (line %3, column %4) cannot be stored in %5.")
                     .arg(codePoint, 4, 16, QLatin1Char('0')).arg(character)
                     .arg(line).arg(int(bad) - lineStart + 1).arg(QString::fromLatin1(format->encoding));
        return false;
    }
    QSaveFile output(identity(path));
    if (!output.open(QIODevice::WriteOnly)) { *error = output.errorString(); return false; }
    const QByteArray bytes(encoded.data(), int(encoded.size()));
    if (output.write(bytes) != bytes.size()) { *error = "Write failed: " + output.errorString(); return false; }
    if (!output.commit()) { *error = "Commit failed: " + output.errorString(); return false; }
    format->digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    return true;
}
