#pragma once
#include "fileio.h"
#include "highlighter.h"
#include "searchindex.h"
#include <QPlainTextEdit>
#include <memory>

struct CharacterMetrics { qint64 characters = 0; };
class OverviewRuler;
class Editor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit Editor(QWidget *parent = nullptr);
    // forcedEncoding empty = detect the encoding automatically.
    bool load(const QString &file, QString *error, const QString &forcedEncoding = QString());
    bool save(const QString &file, QString *error, bool lossy = false, bool *unmappable = nullptr);
    QString encoding() const { return QString::fromLatin1(format.encoding); }
    QString newlineName() const { return format.newline == "\r\n" ? "CRLF" : format.newline == "\r" ? "CR" : "LF"; }
    // Both change how the file is written; the tab becomes modified until it is saved.
    void setEncoding(const QString &id);
    void setNewline(const QByteArray &newline);
    bool changedOnDisk() const { return !filePath.isEmpty() && TextFile::changedOnDisk(filePath, format.digest); }
    QString path() const { return filePath; }
    QString language() const { return languageName; }
    void setEditorFont(const QFont &font);
    qint64 characterTotal() const;
    int cursorColumn() const;
    SyntaxHighlighter *highlighter;
    // Where the Find text occurs in this document; scanned in small steps, see SearchIndex.
    SearchIndex *searchIndex() const { return index; }
    // Colour of the marks on the overview ruler (the Find Color setting) and whether they are shown.
    void setFindMarkers(const QColor &color, bool enabled);
    // Lines are document blocks (0-based). Used by the overview ruler.
    int firstVisibleLine() const;
    int lastVisibleLine() const;
    void scrollToLine(int line);
signals:
    void filesDropped(const QStringList &paths);
    void fontZoomed(const QFont &font);
    void statisticsChanged();
protected:
    void wheelEvent(QWheelEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool viewportEvent(QEvent *event) override;
private:
    void placeRuler();
    void detectLanguage();
    void updateMetrics(int position, int added);
    QString filePath, languageName = "Plain text";
    TextFileFormat format;
    std::shared_ptr<CharacterMetrics> metrics;
    int wheelRemainder = 0;
    SearchIndex *index = nullptr;
    OverviewRuler *ruler = nullptr;
};
