#include "searchindex.h"
#include <QElapsedTimer>
#include <QTextDocument>

SearchIndex::SearchIndex(QTextDocument *document, QObject *parent) : QObject(parent), doc(document) {
    stepTimer.setSingleShot(true);
    stepTimer.setInterval(0);
    restartTimer.setSingleShot(true);
    restartTimer.setInterval(200);   // after edits, wait until typing pauses before scanning again
    connect(&stepTimer, &QTimer::timeout, this, [this] { step(); });
    connect(&restartTimer, &QTimer::timeout, this, [this] { restart(false); });
    connect(doc, &QTextDocument::contentsChange, this, [this](int, int removed, int added) {
        if (removed || added) documentChanged();
    });
}
void SearchIndex::setQuery(const QString &query) {
    if (query == text) return;
    text = query;
    matcher = PhraseMatcher(QVector<ColorRule>{{query, QColor()}});
    restartTimer.stop();
    restart(true);
}
void SearchIndex::documentChanged() {
    // A QTextBlock held across an edit may be invalid, so the running scan is dropped here and the
    // whole document is scanned again once the edits stop. The old marks stay until then.
    stepTimer.stop();
    next = QTextBlock();
    if (text.isEmpty()) return;
    running = true;
    restartTimer.start();
    emit changed();   // same marks, but the counter now shows that a new count is on its way
}
// announce: tell listeners right away that the old result is gone (a new query). After an edit the
// old marks stay on screen until the first step of the new scan reports, which avoids a flash of "0".
void SearchIndex::restart(bool announce) {
    stepTimer.stop();
    index.clear();
    capped = false;
    line = 0;
    next = text.isEmpty() ? QTextBlock() : doc->begin();
    running = next.isValid();
    if (announce) emit changed();
    if (running) stepTimer.start();
}
void SearchIndex::step() {
    QElapsedTimer clock;
    clock.start();
    bool added = false;
    int sinceCheck = 0;
    while (next.isValid()) {
        int count = 0;
        matcher.scan(next.text(), [&count](PhraseMatcher::Match) { ++count; });
        if (count) {
            index.add(line, count);
            added = true;
            if (index.total() >= Limit) { capped = true; next = QTextBlock(); break; }
        }
        next = next.next();
        ++line;
        // Reading the clock is cheap but not free; look at it every few lines only.
        if (++sinceCheck >= 32) {
            sinceCheck = 0;
            if (clock.elapsed() >= StepMilliseconds) break;
        }
    }
    running = next.isValid();
    if (running) stepTimer.start();
    if (added || !running) emit changed();
}
