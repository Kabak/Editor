#pragma once
#include "matchindex.h"
#include <QColor>
#include <QWidget>
#include <cstdint>
#include <vector>
class Editor;
class SearchIndex;

// A narrow strip between the text and the vertical scroll bar. It shows where the Find matches are
// in the whole document (one mark per pixel row, so a million matches cost no more than the strip
// is tall) and which part of the document is on screen. Click or drag to scroll.
class OverviewRuler : public QWidget {
    Q_OBJECT
public:
    static constexpr int Width = 12;
    OverviewRuler(Editor *editor, SearchIndex *index);
    void setMarker(const QColor &color, bool enabled);
    QSize sizeHint() const override { return QSize(Width, 0); }
protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
private:
    void jumpTo(int y);
    Editor *editor;
    SearchIndex *index;
    QColor color = Qt::red;
    bool enabled = true;
    // The marked rows are recomputed only when matches, strip height or line count change.
    std::vector<std::uint8_t> rows;
    std::uint64_t rowsGeneration = 0;
    int rowsHeight = -1, rowsLines = -1;
    bool rowsValid = false;
};
