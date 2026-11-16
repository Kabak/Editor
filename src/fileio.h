#pragma once
#include <QString>
#include <QByteArray>

// encoding: an id from textenc::encodings() ("UTF-8", "Windows-1251", "UTF-16 LE BOM", ...).
struct TextFileFormat { QByteArray encoding = "UTF-8"; QByteArray newline = "\n"; QByteArray digest; };
class TextFile {
public:
    // forcedEncoding empty = detect automatically (BOM, UTF-8, then legacy code pages).
    static bool read(const QString &path, QString *text, TextFileFormat *format, QString *error,
                     const QString &forcedEncoding = QString());
    // With lossy == false nothing is written if a character does not exist in the
    // encoding; *unmappable is then set and *error names the character and position.
    static bool write(const QString &path, const QString &text, TextFileFormat *format, QString *error,
                      bool lossy = false, bool *unmappable = nullptr);
    static bool changedOnDisk(const QString &path, const QByteArray &digest);
    static QString identity(const QString &path);
    static bool samePath(const QString &first, const QString &second);
};
