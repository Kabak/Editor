#include "window.h"
#include "dialogs.h"
#include "encoding.h"
#include "encoding_qt.h"
#include "searchpanel.h"
#include "theme.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDialog>
#include <QHash>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QResizeEvent>
#include <QScreen>
#include <QSplitter>
#include <QTimer>
#include <QStatusBar>
#include <QTabBar>
#include <QToolBar>
#include <QUrl>
#include <QWindow>

Window::Window(const QString &configDirectory, bool restoreSession)
    : tabs(new QTabWidget(this)), splitter(new QSplitter(this)), searchPanel(new SearchPanel(this)),
      statistics(new QLabel(this)), settingsStore(configDirectory), profileStore(settingsStore.directory()), sessionStore(settingsStore.directory()) {
    registerQtCodecs();
    QStringList warnings;
    settings = settingsStore.load(&warnings);
    noRules = std::make_shared<HighlightRules>(QVector<ColorRule>{}, QVector<ColorRule>{});   // files without a profile
    reloadProfiles(&warnings);
    setAcceptDrops(true);
    splitter->addWidget(tabs);
    splitter->addWidget(searchPanel);
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({780, 220});
    setCentralWidget(splitter);
    tabs->setTabsClosable(true);
    tabs->setMovable(true);
    tabs->setMinimumWidth(180);
    statistics->setObjectName("documentStatistics");
    statusBar()->addPermanentWidget(statistics, 1);
    auto file = menuBar()->addMenu("&File");
    toolbar = addToolBar("File and highlighting");
    toolbar->setMovable(false);
    auto action = [this](QMenu *menu, const QString &label, const QKeySequence &key, auto callback) {
        auto item = menu->addAction(label);
        item->setShortcut(key);
        connect(item, &QAction::triggered, this, callback);
        return item;
    };
    toolbar->addAction(action(file, "&New", QKeySequence::New, [this] { newTab(); }));
    toolbar->addAction(action(file, "&Open…", QKeySequence::Open, [this] {
        for (const auto &path : QFileDialog::getOpenFileNames(this, "Open text files", {}, "All files (*);;Text files (*.txt)")) openFile(path);
    }));
    toolbar->addAction(action(file, "&Save", QKeySequence::Save, [this] { save(current()); }));
    action(file, "Save &As…", QKeySequence::SaveAs, [this] { save(current(), true); });
    action(file, "&Close tab", QKeySequence::Close, [this] { closeTab(tabs->currentIndex()); });
    file->addSeparator();
    action(file, "E&xit", QKeySequence::Quit, [this] { close(); });
    auto edit = menuBar()->addMenu("&Edit");
    action(edit, "&Undo", QKeySequence::Undo, [this] { if (current()) current()->undo(); });
    action(edit, "&Redo", QKeySequence::Redo, [this] { if (current()) current()->redo(); });
    edit->addSeparator();
    action(edit, "Cu&t", QKeySequence::Cut, [this] { if (current()) current()->cut(); });
    action(edit, "&Copy", QKeySequence::Copy, [this] { if (current()) current()->copy(); });
    action(edit, "&Paste", QKeySequence::Paste, [this] { if (current()) current()->paste(); });
    action(edit, "Select &All", QKeySequence::SelectAll, [this] { if (current()) current()->selectAll(); });
    action(edit, "&Find Text", QKeySequence::Find, [this] { searchPanel->focusQuery(current() ? current()->textCursor().selectedText() : QString()); });
    action(edit, "Find next", QKeySequence(Qt::Key_F3), [this] { searchPanel->navigate(current(), false); });
    action(edit, "Find previous", QKeySequence(Qt::SHIFT | Qt::Key_F3), [this] { searchPanel->navigate(current(), true); });
    auto encodingMenu = menuBar()->addMenu("E&ncoding");
    // One submenu per group (Unicode, Cyrillic, ...), one item per encoding.
    auto fillEncodings = [this](QMenu *root, std::function<void(const QString &)> choose) {
        QHash<QString, QMenu *> groups;
        for (const textenc::Info &info : textenc::encodings()) {
            const QString group = QString::fromStdString(info.group), id = QString::fromStdString(info.id);
            QMenu *&submenu = groups[group];
            if (!submenu) submenu = root->addMenu(group);
            QAction *item = submenu->addAction(id);
            item->setData(id);
            item->setCheckable(true);
            connect(item, &QAction::triggered, this, [choose, id] { choose(id); });
        }
        connect(root, &QMenu::aboutToShow, this, [this, root] {
            const QString now = current() ? current()->encoding() : QString();
            for (QAction *item : root->findChildren<QAction *>())
                if (item->isCheckable()) item->setChecked(item->data().toString() == now);
        });
    };
    fillEncodings(encodingMenu->addMenu("&Save with encoding (convert this tab)"), [this](const QString &id) { changeEncoding(id); });
    fillEncodings(encodingMenu->addMenu("&Reopen with encoding (re-read the file)"), [this](const QString &id) { reopenWithEncoding(id); });
    action(encodingMenu, "&Open files with encoding...", {}, [this] { openWithEncoding(); });
    encodingMenu->addSeparator();
    auto endings = encodingMenu->addMenu("&Line endings");
    action(endings, "Windows (CRLF)", {}, [this] { if (current()) current()->setNewline("\r\n"); });
    action(endings, "Unix / Linux (LF)", {}, [this] { if (current()) current()->setNewline("\n"); });
    action(endings, "Classic Mac (CR)", {}, [this] { if (current()) current()->setNewline("\r"); });
    auto preferences = menuBar()->addMenu("&Settings");
    action(preferences, "Color Profiles…", {}, [this] { editColorProfiles(); });
    action(preferences, "Editor Font…", {}, [this] {
        bool accepted = false;
        QFont font = QFontDialog::getFont(&accepted, settings.editorFont, this, "Editor Font (6–96 pt)");
        if (!accepted) return;
        font.setPointSizeF(qBound(6.0, font.pointSizeF() > 0 ? font.pointSizeF() : 11.0, 96.0));
        AppSettings candidate = settings;
        candidate.editorFont = font;
        setPreferences(candidate);
    });
    auto appearance = preferences->addMenu("Appearance");
    auto modes = new QActionGroup(this);
    dayAction = action(appearance, "Day", {}, [this] { AppSettings candidate = settings; candidate.night = false; setPreferences(candidate); });
    nightAction = action(appearance, "Night", {}, [this] { AppSettings candidate = settings; candidate.night = true; setPreferences(candidate); });
    for (auto item : {dayAction, nightAction}) { item->setCheckable(true); modes->addAction(item); }
    action(appearance, "Theme Colors…", {}, [this] {
        ThemeColorsDialog dialog(settings, this);
        while (dialog.exec() == QDialog::Accepted) {
            AppSettings candidate = settings;
            candidate.dayColors = dialog.dayColors();
            candidate.nightColors = dialog.nightColors();
            if (setPreferences(candidate)) break;
        }
    });

    auto coloring = menuBar()->addMenu("&Coloring Settings");
    findColorAction = action(coloring, "Find Color…", {}, [this] {
        FindColorDialog dialog(settings.findColor, this);
        while (dialog.exec() == QDialog::Accepted) {
            AppSettings candidate = settings;
            candidate.findColor = dialog.color();
            if (setPreferences(candidate)) break;
        }
    });
    coloring->addSeparator();
    blocksAction = action(coloring, "Blocks Color", {}, [this](bool enabled) { AppSettings candidate = settings; candidate.blocksEnabled = enabled; setPreferences(candidate); });
    phrasesAction = action(coloring, "Phrases Color", {}, [this](bool enabled) { AppSettings candidate = settings; candidate.phrasesEnabled = enabled; setPreferences(candidate); });
    findAction = action(coloring, "Find Color", {}, [this](bool enabled) { AppSettings candidate = settings; candidate.findEnabled = enabled; setPreferences(candidate); });
    toolbar->addSeparator();
    for (auto item : {blocksAction, phrasesAction, findAction}) { item->setCheckable(true); toolbar->addAction(item); }
    blocksAction->setObjectName("blocksEnabled");
    phrasesAction->setObjectName("phrasesEnabled");
    findAction->setObjectName("findEnabled");

    auto view = menuBar()->addMenu("&View");
    auto wrap = view->addMenu("Word Wrap");
    auto wrapGroup = new QActionGroup(this);
    const QStringList values{"automatic", "on", "off"}, labels{"Automatic", "Always", "Never"};
    for (int i = 0; i < values.size(); ++i) {
        const QString value = values[i];
        auto item = action(wrap, labels[i], {}, [this, value] { AppSettings candidate = settings; candidate.wrapMode = value; setPreferences(candidate); });
        item->setCheckable(true);
        item->setData(value);
        wrapGroup->addAction(item);
        wrapActions.append(item);
    }
    connect(tabs, &QTabWidget::tabCloseRequested, this, &Window::closeTab);
    connect(tabs, &QTabWidget::currentChanged, this, [this] {
        if (current()) current()->highlighter->setSearch(searchPanel->query());
        syncSearch();
        searchPanel->clearMessage();
        updateMatchCount();
        refresh();
        syncSession();
    });
    connect(tabs->tabBar(), &QTabBar::tabMoved, this, [this] { syncSession(); });
    connect(searchPanel, &SearchPanel::queryChanged, this, [this](const QString &query) {
        if (current()) current()->highlighter->setSearch(query);
        syncSearch();
        updateMatchCount();
    });
    connect(searchPanel, &SearchPanel::navigationRequested, this, [this](bool backwards) { searchPanel->navigate(current(), backwards); });
    resize(1100, 740);
    restoreGeometry(settings.geometry);
    if (!settings.splitter.isEmpty()) splitter->restoreState(settings.splitter);
    applyPreferences();
    newTab();
    if (restoreSession) {
        const SessionState state = sessionStore.load(&warnings);
        QStringList failed;
        for (const QString &path : state.files) {
            QString error;
            if (!openDocument(path, &error)) failed.append(path + ": " + error);
        }
        for (int i = 0; i < tabs->count(); ++i)
            if (static_cast<Editor *>(tabs->widget(i))->path() == state.active) tabs->setCurrentIndex(i);
        if (!failed.isEmpty()) warnings.append("Some documents could not be reopened and were removed from the session:\n" + failed.join('\n'));
    }
    restoring = false;
    syncSession();
    if (!warnings.isEmpty()) warn("Settings and session recovery", warnings.join("\n\n"));
}
Editor *Window::current() const { return static_cast<Editor *>(tabs->currentWidget()); }
void Window::warn(const QString &title, const QString &message) {
    auto box = new QMessageBox(QMessageBox::Warning, title, message, QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setTextFormat(Qt::PlainText);
    box->open();
}
void Window::attachEditor(Editor *editor) {
    editor->setEditorFont(settings.editorFont);
    editor->highlighter->configure(rulesFor(editor), settings);
    editor->highlighter->setSearch(searchPanel->query());
    connect(editor->document(), &QTextDocument::modificationChanged, this, [this] { refresh(); });
    connect(editor->searchIndex(), &SearchIndex::changed, this, [this, editor] { if (editor == current()) updateMatchCount(); });
    editor->setFindMarkers(settings.findColor, settings.findEnabled);
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this] { refreshStatus(); });
    connect(editor, &Editor::statisticsChanged, this, [this] { refreshStatus(); });
    connect(editor, &Editor::filesDropped, this, [this](const QStringList &paths) { for (const QString &path : paths) openFile(path); });
    connect(editor, &Editor::fontZoomed, this, [this](const QFont &font) { AppSettings candidate = settings; candidate.editorFont = font; setPreferences(candidate); });
}
Editor *Window::newTab() {
    auto editor = new Editor;
    attachEditor(editor);
    tabs->addTab(editor, "Untitled");
    tabs->setCurrentWidget(editor);
    updateWrap();
    editor->setFocus();
    refresh();
    return editor;
}
void Window::refreshStatus() {
    if (auto editor = current()) {
        const auto cursor = editor->textCursor();
        QString syntaxNote;
        if (editor->language() != "Plain text" && editor->document()->characterCount() > 2 * 1024 * 1024)
            syntaxNote = " (syntax colors paused for large file)";
        statistics->setText(QString("Characters: %1    Line: %2    Column: %3    |    %4    |    %5    |    %6%7")
            .arg(editor->characterTotal()).arg(cursor.blockNumber() + 1).arg(editor->cursorColumn())
            .arg(editor->encoding(), editor->newlineName(), editor->language(), syntaxNote));
    }
}
void Window::refresh() {
    for (int i = 0; i < tabs->count(); ++i) {
        auto editor = static_cast<Editor *>(tabs->widget(i));
        QString title = editor->path().isEmpty() ? "Untitled" : QFileInfo(editor->path()).fileName();
        if (editor->document()->isModified()) title += " *";
        tabs->setTabText(i, title);
        tabs->setTabToolTip(i, editor->path());
    }
    if (current()) setWindowTitle(tabs->tabText(tabs->currentIndex()) + " — Text Editor");
    refreshStatus();
}
bool Window::openDocument(const QString &path, QString *error, const QString &forcedEncoding) {
    const QString identity = TextFile::identity(path);
    for (int i = 0; i < tabs->count(); ++i) {
        auto editor = static_cast<Editor *>(tabs->widget(i));
        if (!editor->path().isEmpty() && TextFile::samePath(editor->path(), identity)) { tabs->setCurrentIndex(i); return true; }
    }
    std::unique_ptr<Editor> loaded(new Editor);
    if (!loaded->load(path, error, forcedEncoding)) return false;
    Editor *editor = loaded.release();
    auto blank = current();
    const bool replace = blank && blank->path().isEmpty() && blank->document()->isEmpty() && !blank->document()->isModified();
    attachEditor(editor);
    tabs->addTab(editor, QFileInfo(path).fileName());
    tabs->setCurrentWidget(editor);
    if (replace) { tabs->removeTab(tabs->indexOf(blank)); delete blank; }
    updateWrap();
    editor->setFocus();
    refresh();
    syncSession();
    return true;
}
void Window::changeEncoding(const QString &id) {
    if (!current()) return;
    current()->setEncoding(id);
    refresh();
}
void Window::reopenWithEncoding(const QString &id) {
    Editor *editor = current();
    if (!editor) return;
    if (editor->path().isEmpty()) {
        warn("Reopen with encoding", "This tab is not saved to a file yet, so there is nothing to re-read. Use \"Save with encoding\" instead.");
        return;
    }
    if (editor->document()->isModified()) {
        QMessageBox ask(QMessageBox::Warning, "Reopen with encoding",
            "Discard the unsaved changes in this tab and re-read the file from disk as " + id + "?",
            QMessageBox::Discard | QMessageBox::Cancel, this);
        ask.setDefaultButton(QMessageBox::Cancel);
        if (ask.exec() != QMessageBox::Discard) return;
    }
    QString error;
    if (!editor->load(editor->path(), &error, id)) { warn("Cannot reopen file", editor->path() + "\n" + error); return; }
    refresh();
}
void Window::openWithEncoding() {
    QStringList ids;
    for (const textenc::Info &info : textenc::encodings()) ids << QString::fromStdString(info.id);
    bool accepted = false;
    const QString id = QInputDialog::getItem(this, "Open with encoding", "Read the files as:", ids, 0, false, &accepted);
    if (!accepted) return;
    for (const QString &path : QFileDialog::getOpenFileNames(this, "Open text files as " + id, {}, "All files (*)")) {
        QString error;
        if (!openDocument(path, &error, id)) warn("Cannot open file", path + "\n" + error);
    }
}
void Window::openFile(const QString &path) {
    QString error;
    if (!openDocument(path, &error)) warn("Cannot open file", path + "\n" + error);
}
bool Window::save(Editor *editor, bool saveAs) {
    if (!editor) return false;
    QString path = editor->path();
    if (path.isEmpty() || saveAs) {
        QFileDialog dialog(this, "Save UTF-8 text file");
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.setFileMode(QFileDialog::AnyFile);
        dialog.setNameFilters({"Text files (*.txt)", "All files (*)"});
        dialog.setDefaultSuffix("txt");
        dialog.selectFile(path.isEmpty() ? "Untitled.txt" : path);
        if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return false;
        path = dialog.selectedFiles().first();
    }
    const QString identity = TextFile::identity(path);
    if (!editor->path().isEmpty() && TextFile::samePath(editor->path(), identity) && editor->changedOnDisk()) {
        QMessageBox confirmation(QMessageBox::Warning, "File changed on disk",
            "This file was changed, removed, or became inaccessible since it was opened. Overwrite it with the text in this tab?",
            QMessageBox::Save | QMessageBox::Cancel, this);
        confirmation.setDefaultButton(QMessageBox::Cancel);
        if (confirmation.exec() != QMessageBox::Save) return false;
    }
    for (int i = 0; i < tabs->count(); ++i) {
        auto other = static_cast<Editor *>(tabs->widget(i));
        if (other != editor && !other->path().isEmpty() && TextFile::samePath(other->path(), identity)) {
            warn("File already open", "Close the other tab for this file before overwriting it.");
            return false;
        }
    }
    QString error;
    bool unmappable = false;
    if (!editor->save(path, &error, false, &unmappable)) {
        if (!unmappable) { warn("Cannot save file", path + "\n" + error); return false; }
        QMessageBox ask(QMessageBox::Warning, "Characters not supported by " + editor->encoding(),
            error + "\n\nSave anyway and replace every unsupported character with '?'\n(the file on disk would lose them)? "
            "Or cancel and pick another encoding, for example UTF-8, in the Encoding menu.", QMessageBox::Save | QMessageBox::Cancel, this);
        ask.setTextFormat(Qt::PlainText);
        ask.setDefaultButton(QMessageBox::Cancel);
        if (ask.exec() != QMessageBox::Save) return false;
        if (!editor->save(path, &error, true, &unmappable)) { warn("Cannot save file", path + "\n" + error); return false; }
    }
    editor->highlighter->configure(rulesFor(editor), settings);
    refresh();
    syncSession();
    return true;
}
bool Window::mayClose(Editor *editor) {
    if (!editor || !editor->document()->isModified()) return true;
    QMessageBox dialog(QMessageBox::Warning, "Unsaved changes", "Save changes to " + (editor->path().isEmpty() ? QString("Untitled") : editor->path()) + "?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    dialog.setTextFormat(Qt::PlainText);
    dialog.setDefaultButton(QMessageBox::Save);
    const int answer = dialog.exec();
    return answer == QMessageBox::Discard || (answer == QMessageBox::Save && save(editor));
}
void Window::closeTab(int index) {
    auto editor = static_cast<Editor *>(tabs->widget(index));
    if (!editor || !mayClose(editor)) return;
    const bool wasRestoring = restoring;
    restoring = true;
    tabs->removeTab(index);
    delete editor;
    if (tabs->count() == 0) newTab();
    restoring = wasRestoring;
    refresh();
    syncSession();
}
SessionState Window::sessionState() const {
    SessionState state;
    for (int i = 0; i < tabs->count(); ++i) {
        const QString path = static_cast<Editor *>(tabs->widget(i))->path();
        if (!path.isEmpty()) state.files.append(path);
    }
    if (current()) state.active = current()->path();
    return state;
}
void Window::syncSession() {
    if (restoring || closing) return;
    QString error;
    if (!sessionStore.save(sessionState(), &error)) {
        if (lastSessionError != error) warn("Cannot save open documents list", error);
        lastSessionError = error;
    } else lastSessionError.clear();
}
// Loads every profile that an extension is bound to. A missing or unreadable profile is skipped,
// so files with that extension get no Blocks/Phrases coloring.
void Window::reloadProfiles(QStringList *warnings) {
    profileRules.clear();
    for (const ProfileBinding &binding : settings.bindings) {
        const QString key = binding.profile.toLower();
        if (profileRules.contains(key)) continue;
        ColorProfile profile;
        QStringList problems;
        if (profileStore.load(binding.profile, &profile, &problems)) profileRules.insert(key, std::make_shared<HighlightRules>(profile.blocks, profile.phrases));
        if (warnings) warnings->append(problems);
    }
}
std::shared_ptr<const HighlightRules> Window::rulesFor(const Editor *editor) const {
    const QString extension = ProfileStore::normalizeExtension(QFileInfo(editor->path()).suffix());
    if (!extension.isEmpty()) {
        for (const ProfileBinding &binding : settings.bindings) {
            if (binding.extension != extension) continue;
            const auto found = profileRules.constFind(binding.profile.toLower());
            if (found != profileRules.constEnd()) return found.value();
            break;
        }
    }
    return noRules;
}
void Window::editColorProfiles() {
    ColorProfilesDialog dialog(settings.bindings, profileStore,
        [this](const QVector<ProfileBinding> &bindings) {
            AppSettings candidate = settings;
            candidate.bindings = bindings;
            return setPreferences(candidate);
        },
        [this] { reloadProfiles(nullptr); applyPreferences(); },     // a profile file was saved
        this);
    dialog.exec();
}
bool Window::setPreferences(const AppSettings &candidate) {
    QString error;
    if (!settingsStore.save(candidate, &error)) {
        QMessageBox warning(QMessageBox::Warning, "Cannot save settings", error, QMessageBox::Ok, this);
        warning.setTextFormat(Qt::PlainText);
        warning.exec();
        applyPreferences();
        return false;
    }
    const bool bindingsChanged = settings.bindings != candidate.bindings;
    settings = candidate;
    if (bindingsChanged) reloadProfiles(nullptr);
    applyPreferences();
    return true;
}
void Window::applyPreferences() {
    Theme::apply(settings);
    blocksAction->setChecked(settings.blocksEnabled);
    phrasesAction->setChecked(settings.phrasesEnabled);
    findAction->setChecked(settings.findEnabled);
    findColorAction->setIcon(colorIcon(settings.findColor));
    dayAction->setChecked(!settings.night);
    nightAction->setChecked(settings.night);
    for (auto action : wrapActions) action->setChecked(action->data().toString() == settings.wrapMode);
    for (int i = 0; i < tabs->count(); ++i) {
        auto editor = static_cast<Editor *>(tabs->widget(i));
        editor->setEditorFont(settings.editorFont);
        editor->highlighter->configure(rulesFor(editor), settings);
        editor->setFindMarkers(settings.findColor, settings.findEnabled);
    }
    styleToolbarButtons();
    updateWrap();
}
// Blocks/Phrases/Find Color buttons: clearly different default and pressed looks in every theme.
void Window::styleToolbarButtons() {
    if (!toolbar) return;
    const ThemeColors &colors = settings.night ? settings.nightColors : settings.dayColors;
    const QString style = QString(
        "QToolButton { background-color: %1; color: %2; border: 2px solid %3; border-radius: 4px; padding: 3px 8px; }"
        "QToolButton:hover { border-color: %4; }"
        "QToolButton:checked, QToolButton:pressed { background-color: %5; color: %6; border: 2px solid %4; font-weight: bold; }")
        .arg(colors.buttonOff.name(), readableForeground(colors.buttonOff).name(), colors.buttonOff.darker(140).name(),
             readableForeground(colors.buttonOn).name(), colors.buttonOn.name(), readableForeground(colors.buttonOn).name());
    for (QAction *item : {blocksAction, phrasesAction, findAction})
        if (QWidget *button = toolbar->widgetForAction(item)) button->setStyleSheet(style);
}
// Only the visible tab searches: scanning documents nobody is looking at would waste time and memory.
void Window::syncSearch() {
    for (int i = 0; i < tabs->count(); ++i) {
        auto editor = static_cast<Editor *>(tabs->widget(i));
        editor->searchIndex()->setQuery(editor == current() ? searchPanel->query() : QString());
    }
}
void Window::updateMatchCount() {
    Editor *editor = current();
    if (!editor || searchPanel->query().isEmpty()) { searchPanel->setMatchCount(-1); return; }
    const SearchIndex *found = editor->searchIndex();
    searchPanel->setMatchCount(found->total(), found->searching(), found->truncated());
}
void Window::updateWrap() {
    if (!tabs) return;
    QScreen *screen = windowHandle() ? windowHandle()->screen() : QApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    const bool smaller = !isMaximized() && !isFullScreen() && (frameGeometry().width() < available.width() || frameGeometry().height() < available.height());
    const bool wrap = settings.wrapMode == "on" || (settings.wrapMode == "automatic" && smaller);
    for (int i = 0; i < tabs->count(); ++i) {
        auto editor = static_cast<Editor *>(tabs->widget(i));
        editor->setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
        editor->setHorizontalScrollBarPolicy(wrap ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    }
}
void Window::resizeEvent(QResizeEvent *event) { QMainWindow::resizeEvent(event); updateWrap(); }
void Window::changeEvent(QEvent *event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) updateWrap();
}
void Window::dragEnterEvent(QDragEnterEvent *event) { if (event->mimeData()->hasUrls()) event->acceptProposedAction(); }
void Window::dropEvent(QDropEvent *event) {
    if (!event->mimeData()->hasUrls()) return;
    for (const QUrl &url : event->mimeData()->urls()) openFile(url.isLocalFile() ? url.toLocalFile() : url.toString());
    event->acceptProposedAction();
}
void Window::closeEvent(QCloseEvent *event) {
    closing = true;
    Editor *active = current();
    for (int i = 0; i < tabs->count(); ++i) {
        tabs->setCurrentIndex(i);
        if (!mayClose(current())) { closing = false; syncSession(); event->ignore(); return; }
    }
    if (active) tabs->setCurrentWidget(active);
    settings.geometry = saveGeometry();
    settings.splitter = splitter->saveState();
    QString error;
    for (;;) {
        if (settingsStore.save(settings, &error) && sessionStore.save(sessionState(), &error)) break;
        const auto answer = QMessageBox::warning(this, "Cannot save application state", error + "\n\nRetry, cancel closing, or close without saving application settings/session?",
            QMessageBox::Retry | QMessageBox::Cancel | QMessageBox::Ignore, QMessageBox::Retry);
        if (answer == QMessageBox::Cancel) { closing = false; event->ignore(); return; }
        if (answer == QMessageBox::Ignore) break;
    }
    event->accept();
}
