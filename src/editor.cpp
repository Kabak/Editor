#include "editor.h"
#include "overviewruler.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMimeData>
#include <QScrollBar>
#include <QTextBlock>
#include <QUrl>
#include <QWheelEvent>

namespace {
int scalarCount(const QString &text) {
    int count = 0;
    for (int i = 0; i < text.size(); ++i) {
        ++count;
        if (text[i].isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate()) ++i;
    }
    return count;
}
struct BlockMetrics : QTextBlockUserData {
    std::shared_ptr<CharacterMetrics> total;
    int count;
    BlockMetrics(std::shared_ptr<CharacterMetrics> value, int size) : total(std::move(value)), count(size) { total->characters += count; }
    ~BlockMetrics() override { total->characters -= count; }
};
}
Editor::Editor(QWidget *parent) : QPlainTextEdit(parent), metrics(std::make_shared<CharacterMetrics>()) {
    setEditorFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    connect(document(), &QTextDocument::contentsChange, this, [this](int position, int removed, int added) {
        if (!removed && !added) return;
        updateMetrics(position, added);
        emit statisticsChanged();
    });
    highlighter = new SyntaxHighlighter(document());
    index = new SearchIndex(document(), this);
    // The ruler sits in a margin between the text and the scroll bar, so it never covers text.
    ruler = new OverviewRuler(this, index);
    setViewportMargins(0, 0, OverviewRuler::Width, 0);
}
void Editor::setFindMarkers(const QColor &color, bool enabled) { ruler->setMarker(color, enabled); }
int Editor::firstVisibleLine() const { return firstVisibleBlock().blockNumber(); }
int Editor::lastVisibleLine() const {
    return cursorForPosition(QPoint(0, qMax(0, viewport()->height() - 1))).blockNumber();
}
void Editor::scrollToLine(int line) {
    // The scroll bar counts screen lines, so with wrapped lines this is approximate; without wrapping it is exact.
    QScrollBar *bar = verticalScrollBar();
    const qint64 lines = qMax(1, document()->blockCount());
    const qint64 span = qint64(bar->maximum()) + bar->pageStep();
    const qint64 target = qBound<qint64>(0, line, lines - 1) * span / lines - bar->pageStep() / 2;
    bar->setValue(int(qBound<qint64>(bar->minimum(), target, bar->maximum())));
}
void Editor::placeRuler() {
    const QRect area = viewport()->geometry();
    ruler->setGeometry(area.right() + 1, area.top(), OverviewRuler::Width, area.height());
}
bool Editor::viewportEvent(QEvent *event) {
    const bool handled = QPlainTextEdit::viewportEvent(event);
    // The viewport moves when the window is resized or a scroll bar appears or disappears.
    if (event->type() == QEvent::Resize || event->type() == QEvent::Move || event->type() == QEvent::Show) placeRuler();
    return handled;
}
void Editor::updateMetrics(int position, int added) {
    QTextBlock block = document()->findBlock(qMin(position, document()->characterCount() - 1));
    const int end = position + added;
    while (block.isValid()) {
        const int count = scalarCount(block.text());
        auto data = static_cast<BlockMetrics *>(block.userData());
        if (data) { metrics->characters += count - data->count; data->count = count; }
        else block.setUserData(new BlockMetrics(metrics, count));
        block = block.next();
        if (block.position() > end) break;
    }
}
qint64 Editor::characterTotal() const { return metrics->characters + document()->blockCount() - 1; }
int Editor::cursorColumn() const { return scalarCount(textCursor().block().text().left(textCursor().positionInBlock())) + 1; }
void Editor::setEditorFont(const QFont &value) {
    QFont changed = value;
    changed.setPointSizeF(qBound(6.0, changed.pointSizeF() > 0 ? changed.pointSizeF() : 11.0, 96.0));
    if (font() != changed) setFont(changed);
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
}
void Editor::wheelEvent(QWheelEvent *event) {
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        wheelRemainder += event->angleDelta().y();
        int steps = wheelRemainder / 120;
        wheelRemainder %= 120;
        if (!steps && !event->pixelDelta().isNull()) steps = event->pixelDelta().y() > 0 ? 1 : -1;
        if (steps) {
            QFont changed = font();
            changed.setPointSizeF(qBound(6.0, changed.pointSizeF() + steps, 96.0));
            setEditorFont(changed);
            emit fontZoomed(changed);
        }
        event->accept();
    } else { wheelRemainder = 0; QPlainTextEdit::wheelEvent(event); }
}
void Editor::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
    else QPlainTextEdit::dragEnterEvent(event);
}
void Editor::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
    else QPlainTextEdit::dragMoveEvent(event);
}
void Editor::dropEvent(QDropEvent *event) {
    if (!event->mimeData()->hasUrls()) { QPlainTextEdit::dropEvent(event); return; }
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) paths.append(url.isLocalFile() ? url.toLocalFile() : url.toString());
    emit filesDropped(paths);
    event->acceptProposedAction();
}
void Editor::detectLanguage() {
    const QString ext = QFileInfo(filePath).suffix().toLower();
    if (QStringList{"c", "h", "cpp", "hpp", "cc", "hh", "cxx", "hxx"}.contains(ext)) languageName = "C/C++";
    else if (ext == "json") languageName = "JSON";
    else languageName = "Plain text";
    highlighter->setLanguage(languageName);
}
bool Editor::load(const QString &file, QString *error, const QString &forcedEncoding) {
    QString text;
    TextFileFormat candidate;
    if (!TextFile::read(file, &text, &candidate, error, forcedEncoding)) return false;
    setPlainText(text);
    updateMetrics(0, document()->characterCount());
    format = candidate;
    filePath = TextFile::identity(file);
    document()->setModified(false);
    detectLanguage();
    emit statisticsChanged();
    return true;
}
void Editor::setEncoding(const QString &id) {
    const QByteArray value = id.toLatin1();
    if (value == format.encoding) return;
    format.encoding = value;
    document()->setModified(true);
    emit statisticsChanged();
}
void Editor::setNewline(const QByteArray &newline) {
    if (newline == format.newline) return;
    format.newline = newline;
    document()->setModified(true);
    emit statisticsChanged();
}
bool Editor::save(const QString &file, QString *error, bool lossy, bool *unmappable) {
    if (!TextFile::write(file, toPlainText(), &format, error, lossy, unmappable)) return false;
    filePath = TextFile::identity(file);
    document()->setModified(false);
    detectLanguage();
    return true;
}
