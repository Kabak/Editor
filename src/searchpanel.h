#pragma once
#include <QWidget>
class QLineEdit;
class QLabel;
class Editor;
class SearchPanel : public QWidget {
    Q_OBJECT
public:
    explicit SearchPanel(QWidget *parent = nullptr);
    QString query() const;
    void focusQuery(const QString &selection = QString());
    void navigate(Editor *editor, bool backwards);
    void clearMessage();
    // Total matches in the current document; a negative value clears the counter.
    // searching: the scan is still running, so the number can grow; truncated: counting stopped at the limit.
    void setMatchCount(qint64 count, bool searching = false, bool truncated = false);
signals:
    void queryChanged(const QString &query);
    void navigationRequested(bool backwards);
private:
    QLineEdit *input;
    QLabel *message, *count;
};
