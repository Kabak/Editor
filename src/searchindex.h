#pragma once
#include "matchindex.h"
#include "rules.h"
#include <QObject>
#include <QTextBlock>
#include <QTimer>
class QTextDocument;

// Finds every match of the Find text in short, time-limited steps in the UI thread, so typing and
// scrolling stay responsive on any file size: a step stops after a few milliseconds, hands control
// back to the event loop and continues from the same line. Matching is the same case-insensitive
// automaton that highlights the text, so counts agree with what is painted and with F3.
class SearchIndex : public QObject {
    Q_OBJECT
public:
    static constexpr qint64 Limit = 200000;   // stop counting here; the counter then shows "n+"
    static constexpr int StepMilliseconds = 4;
    explicit SearchIndex(QTextDocument *document, QObject *parent = nullptr);
    // An empty query clears the index and stops any running scan.
    void setQuery(const QString &query);
    QString query() const { return text; }
    const MatchIndex &matches() const { return index; }
    qint64 total() const { return index.total(); }
    // True while a scan is running or waiting for the document to stop changing.
    bool searching() const { return running; }
    bool truncated() const { return capped; }
signals:
    void changed();   // matches were added, the scan finished, or the index was cleared
private:
    void restart(bool announce);
    void step();
    void documentChanged();
    QTextDocument *doc;
    QString text;
    PhraseMatcher matcher;
    MatchIndex index;
    QTextBlock next;
    int line = 0;
    QTimer stepTimer, restartTimer;
    bool running = false, capped = false;
};
