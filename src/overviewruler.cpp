#include "overviewruler.h"
#include "editor.h"
#include "searchindex.h"
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

OverviewRuler::OverviewRuler(Editor *parentEditor, SearchIndex *matches) : QWidget(parentEditor), editor(parentEditor), index(matches) {
    setToolTip("Find matches in the whole document. Click or drag to scroll.");
    setCursor(Qt::PointingHandCursor);
    connect(index, &SearchIndex::changed, this, [this] { update(); });
    connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { update(); });
    connect(editor->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] { update(); });
    connect(editor->document(), &QTextDocument::blockCountChanged, this, [this] { update(); });
}
void OverviewRuler::setMarker(const QColor &value, bool on) {
    if (color == value && enabled == on) return;
    color = value;
    enabled = on;
    update();
}
void OverviewRuler::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    const QColor background = palette().color(QPalette::Window);
    painter.fillRect(rect(), background);
    const int height = this->height(), lines = editor->document()->blockCount();
    if (height <= 0 || lines <= 0) return;
    // The part of the document that is on screen, drawn under the marks.
    const int top = MatchIndex::rowForLine(editor->firstVisibleLine(), lines, height);
    const int bottom = MatchIndex::rowForLine(editor->lastVisibleLine(), lines, height);
    QColor shade = palette().color(QPalette::WindowText);
    shade.setAlpha(48);
    painter.fillRect(0, top, Width, qMax(4, bottom - top + 1), shade);
    if (!enabled || index->query().isEmpty()) return;
    const MatchIndex &matches = index->matches();
    if (!rowsValid || rowsGeneration != matches.generation() || rowsHeight != height || rowsLines != lines) {
        matches.rows(lines, height, &rows);
        rowsGeneration = matches.generation();
        rowsHeight = height;
        rowsLines = lines;
        rowsValid = true;
    }
    // The marks use the Find Color exactly as set; they are swatches, not text, so no contrast substitution.
    const QColor mark = color;
    // Neighbouring marked rows are drawn as one rectangle; a lone mark is two pixels tall so it stays visible.
    for (int y = 0; y < height;) {
        if (!rows[static_cast<std::size_t>(y)]) { ++y; continue; }
        const int start = y;
        while (y < height && rows[static_cast<std::size_t>(y)]) ++y;
        painter.fillRect(2, start, Width - 4, qMax(2, y - start), mark);
    }
}
void OverviewRuler::jumpTo(int y) {
    editor->scrollToLine(MatchIndex::lineForRow(y, editor->document()->blockCount(), height()));
}
void OverviewRuler::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) jumpTo(event->pos().y());
    else QWidget::mousePressEvent(event);
}
void OverviewRuler::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) jumpTo(event->pos().y());
    else QWidget::mouseMoveEvent(event);
}
