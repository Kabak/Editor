#include "dialogs.h"
#include "fileio.h"
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>

namespace {
class RuleDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (auto line = qobject_cast<QLineEdit *>(editor)) line->setMaxLength(std::numeric_limits<int>::max());
        return editor;
    }
};
// Drop-down editor for the "color profile" column; it opens its list as soon as the cell is clicked.
class ProfileComboDelegate : public QStyledItemDelegate {
public:
    ProfileComboDelegate(const ProfileStore &profiles, QObject *parent) : QStyledItemDelegate(parent), store(profiles) {}
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override {
        auto combo = new QComboBox(parent);
        combo->addItems(store.names());
        auto self = const_cast<ProfileComboDelegate *>(this);
        connect(combo, QOverload<int>::of(&QComboBox::activated), self, [self, combo] {
            emit self->commitData(combo);
            emit self->closeEditor(combo);
        });
        QTimer::singleShot(0, combo, &QComboBox::showPopup);
        return combo;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        auto combo = static_cast<QComboBox *>(editor);
        const QString value = index.data(Qt::EditRole).toString();
        int found = combo->findText(value, Qt::MatchFixedString);
        if (found < 0 && !value.isEmpty()) { combo->addItem(value); found = combo->count() - 1; }
        combo->setCurrentIndex(found);
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override {
        model->setData(index, static_cast<QComboBox *>(editor)->currentText());
    }
private:
    const ProfileStore &store;
};
QFrame *separator(QWidget *parent) {
    auto line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}
QMessageBox::StandardButton askSave(QWidget *parent) {
    return QMessageBox::question(parent, "Save coloring settings?", "Save the changes to these coloring settings?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
}
}
QIcon colorIcon(const QColor &color) {
    QPixmap swatch(24, 24);
    swatch.fill(color);
    QPainter painter(&swatch);
    painter.setPen(Qt::gray);
    painter.drawRect(0, 0, 23, 23);
    return QIcon(swatch);
}
RuleTableModel::RuleTableModel(const QVector<ColorRule> &rules, QObject *parent) : QAbstractTableModel(parent), values(rules) {}
int RuleTableModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : values.size(); }
int RuleTableModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : 2; }
QVariant RuleTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= values.size()) return {};
    const auto &rule = values[index.row()];
    if (role == Qt::DecorationRole && index.column() == 1) return colorIcon(rule.color);
    if (role == Qt::DisplayRole || role == Qt::EditRole) return index.column() == 0 ? rule.text : rule.color.name();
    if (role == Qt::ToolTipRole) return index.column() == 0 ? QString("Double-click to edit the text") : QString("Click to choose a font color");
    return {};
}
QVariant RuleTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) return section == 0 ? QString("Text") : QString("Font color");
    return QAbstractTableModel::headerData(section, orientation, role);
}
Qt::ItemFlags RuleTableModel::flags(const QModelIndex &index) const {
    return QAbstractTableModel::flags(index) | (index.isValid() && index.column() == 0 ? Qt::ItemIsEditable : Qt::NoItemFlags);
}
bool RuleTableModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (!index.isValid() || index.row() < 0 || index.row() >= values.size() || role != Qt::EditRole) return false;
    if (index.column() == 0) values[index.row()].text = value.toString();
    else {
        QColor color(value.toString());
        if (!color.isValid()) return false;
        color.setAlpha(255);
        values[index.row()].color = color;
    }
    emit dataChanged(index, index);
    return true;
}
void RuleTableModel::add() {
    const int row = values.size();
    beginInsertRows({}, row, row);
    values.append({QString(), QColor("#ffdc80")});
    endInsertRows();
}
void RuleTableModel::remove(int row) {
    if (row < 0 || row >= values.size()) return;
    beginRemoveRows({}, row, row);
    values.removeAt(row);
    endRemoveRows();
}
RulesEditor::RulesEditor(const QVector<ColorRule> &rules, bool blocks, QWidget *parent)
    : QWidget(parent), model(new RuleTableModel(rules, this)) {
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto help = new QLabel(blocks
        ? "Color the font of a paragraph starting with the specified text (case-sensitive). A blank line ends the block. The longest prefix wins; ties use the later rule."
        : "Color the font of matching text within a line, ignoring case. Spaces are allowed. Later rules win when phrases overlap.", this);
    help->setWordWrap(true);
    layout->addWidget(help);
    // The table shows the model through a proxy so the filter never touches the stored rules.
    auto proxy = new QSortFilterProxyModel(this);
    proxy->setSourceModel(model);
    proxy->setFilterKeyColumn(0);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setDynamicSortFilter(false);              // a row being edited must not vanish mid-edit
    auto table = new QTableView(this);
    table->setModel(proxy);
    table->setItemDelegate(new RuleDelegate(table));
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table->setColumnWidth(1, 115);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    layout->addWidget(table);
    connect(table, &QTableView::clicked, this, [this](const QModelIndex &index) {
        if (index.column() != 1) return;
        const QModelIndex source = static_cast<const QSortFilterProxyModel *>(index.model())->mapToSource(index);
        const QColor color = QColorDialog::getColor(QColor(model->data(source, Qt::EditRole).toString()), this, "Choose font color");
        if (color.isValid()) model->setData(source, color.name());
    });
    // Filter field, then the rule buttons (the owner may add more buttons to this row).
    row = new QHBoxLayout;
    auto filter = new QLineEdit(this);
    filter->setObjectName("ruleFilter");
    filter->setPlaceholderText("Filter rules by name (ignores case)");
    filter->setClearButtonEnabled(true);
    filter->setAccessibleName("Filter rules by name");
    row->addWidget(filter, 1);
    auto add = new QPushButton("Add rule", this);
    auto remove = new QPushButton("Remove selected", this);
    row->addWidget(add);
    row->addWidget(remove);
    connect(filter, &QLineEdit::textChanged, this, [proxy](const QString &text) { proxy->setFilterFixedString(text); });
    connect(add, &QPushButton::clicked, this, [this, table, proxy, filter] {
        filter->clear();                              // a new empty rule would not match an active filter
        proxy->setFilterFixedString(QString());
        model->add();
        const QModelIndex index = proxy->mapFromSource(model->index(model->rowCount() - 1, 0));
        table->setCurrentIndex(index);
        table->scrollTo(index);
        table->edit(index);
    });
    connect(remove, &QPushButton::clicked, this, [this, table, proxy] {
        QModelIndexList selected = table->selectionModel()->selectedRows();
        QVector<int> sourceRows;
        for (const auto &index : selected) sourceRows.append(proxy->mapToSource(index).row());
        std::sort(sourceRows.begin(), sourceRows.end(), [](int a, int b) { return a > b; });
        for (int sourceRow : sourceRows) model->remove(sourceRow);
    });
    layout->addLayout(row);
}
void RulesEditor::commitEdit() {
    // Commit an active cell editor before the rules are read or compared.
    if (QWidget *focused = window()->focusWidget()) focused->clearFocus();
}
bool RulesEditor::validate() {
    for (const auto &rule : model->rules()) {
        if (rule.text.isEmpty() || rule.text.contains('\n') || rule.text.contains('\r') || rule.text.contains(QChar(0x2028)) || rule.text.contains(QChar(0x2029))) {
            QMessageBox::warning(this, "Invalid rule", "Rules must contain nonempty text on a single line. Edit or remove the empty rule before saving.");
            return false;
        }
    }
    return true;
}
RulesDialog::RulesDialog(const QString &title, const QVector<ColorRule> &rules, bool blocks, QWidget *parent)
    : QDialog(parent), editor(new RulesEditor(rules, blocks, this)), original(rules) {
    setWindowTitle(title);
    resize(640, 450);
    auto layout = new QVBoxLayout(this);
    layout->addWidget(editor);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    editor->buttonRow()->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
QVector<ColorRule> RulesDialog::rules() const { return editor->rules(); }
void RulesDialog::done(int) {
    editor->commitEdit();
    if (editor->rules() == original) { QDialog::done(QDialog::Rejected); return; }
    const auto answer = askSave(this);
    if (answer == QMessageBox::Cancel) return;
    if (answer == QMessageBox::Discard) { QDialog::done(QDialog::Rejected); return; }
    if (!editor->validate()) return;
    QDialog::done(QDialog::Accepted);
}
FindColorDialog::FindColorDialog(const QColor &color, QWidget *parent) : QDialog(parent), original(color), selected(color) {
    setWindowTitle("Find Color");
    auto layout = new QVBoxLayout(this);
    auto button = new QPushButton(colorIcon(selected), selected.name(), this);
    button->setAccessibleName("Find highlight color");
    layout->addWidget(button);
    connect(button, &QPushButton::clicked, this, [this, button] {
        const QColor changed = QColorDialog::getColor(selected, this, "Find highlight color");
        if (!changed.isValid()) return;
        selected = changed;
        button->setIcon(colorIcon(selected));
        button->setText(selected.name());
    });
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
void FindColorDialog::done(int) {
    if (selected == original) { QDialog::done(QDialog::Rejected); return; }
    const auto answer = askSave(this);
    if (answer != QMessageBox::Cancel) QDialog::done(answer == QMessageBox::Save ? QDialog::Accepted : QDialog::Rejected);
}
ThemeColorsDialog::ThemeColorsDialog(const AppSettings &settings, QWidget *parent)
    : QDialog(parent), originalDay(settings.dayColors), originalNight(settings.nightColors),
      selectedDay(originalDay), selectedNight(originalNight) {
    setWindowTitle("Day / Night Colors");
    auto layout = new QVBoxLayout(this);
    auto form = new QFormLayout;
    layout->addLayout(form);
    auto add = [this, form](const QString &label, QColor *color) {
        auto button = new QPushButton(colorIcon(*color), color->name(), this);
        button->setAccessibleName(label);
        form->addRow(label, button);
        connect(button, &QPushButton::clicked, this, [this, button, color, label] {
            const QColor changed = QColorDialog::getColor(*color, this, label);
            if (!changed.isValid()) return;
            *color = changed;
            button->setIcon(colorIcon(*color));
            button->setText(color->name());
        });
    };
    add("Day: editor background", &selectedDay.editor);
    add("Day: menus and window", &selectedDay.window);
    add("Day: selection", &selectedDay.selection);
    add("Night: editor background", &selectedNight.editor);
    add("Night: menus and window", &selectedNight.window);
    add("Night: selection", &selectedNight.selection);
    add("Day: toolbar buttons, default", &selectedDay.buttonOff);
    add("Day: toolbar buttons, pressed", &selectedDay.buttonOn);
    add("Night: toolbar buttons, default", &selectedNight.buttonOff);
    add("Night: toolbar buttons, pressed", &selectedNight.buttonOn);
    auto hint = new QLabel("Toolbar buttons are Blocks Color, Phrases Color and Find Color. Text colors adapt automatically to keep text readable on the chosen backgrounds.", this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
void ThemeColorsDialog::done(int) {
    if (selectedDay == originalDay && selectedNight == originalNight) { QDialog::done(QDialog::Rejected); return; }
    const auto answer = QMessageBox::question(this, "Save theme colors?", "Save these Day and Night colors?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer != QMessageBox::Cancel) QDialog::done(answer == QMessageBox::Save ? QDialog::Accepted : QDialog::Rejected);
}

// ---- color profile editor -----------------------------------------------------
ProfileEditorDialog::ProfileEditorDialog(const ProfileStore &profiles, const QString &profileName, const ColorProfile &profile,
                                         std::function<void()> saved, QWidget *parent)
    : QDialog(parent), store(profiles), name(profileName), blocks(new RulesEditor(profile.blocks, true, this)),
      phrases(new RulesEditor(profile.phrases, false, this)), baseline(profile), onSaved(std::move(saved)) {
    setWindowTitle(name.isEmpty() ? QString("New Color Profile") : "Edit Color Profile: " + name);
    resize(700, 540);
    auto layout = new QVBoxLayout(this);
    auto tabs = new QTabWidget(this);
    tabs->addTab(phrases, "Phrases Color");           // first, and the one shown when the editor opens
    tabs->addTab(blocks, "Blocks Color");
    tabs->setCurrentIndex(0);
    layout->addWidget(tabs, 1);
    layout->addWidget(separator(this));
    auto row = new QHBoxLayout;
    row->addStretch();
    auto save = new QPushButton("Save Profile", this);
    auto close = new QPushButton("Close", this);
    row->addWidget(save);
    row->addWidget(close);
    layout->addLayout(row);
    connect(save, &QPushButton::clicked, this, [this] { saveProfile(); });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
}
ColorProfile ProfileEditorDialog::current() const { return {blocks->rules(), phrases->rules()}; }
bool ProfileEditorDialog::saveProfile() {
    blocks->commitEdit();
    phrases->commitEdit();
    if (!blocks->validate() || !phrases->validate()) return false;
    QDir().mkpath(store.directory());
    const QString suggestion = QDir(store.directory()).filePath((name.isEmpty() ? QString("NewProfile") : name) + ".prf");
    QString path = QFileDialog::getSaveFileName(this, "Save Color Profile", suggestion, "Color profiles (*.prf)");
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().compare("prf", Qt::CaseInsensitive) != 0) path += ".prf";
    const QFileInfo info(path);
    if (!TextFile::samePath(info.absolutePath(), store.directory())) {
        QMessageBox::warning(this, "Save Color Profile", "Color profiles must be saved in the Profiles folder:\n" + store.directory());
        return false;
    }
    const QString newName = info.completeBaseName();
    QString error;
    if (!store.save(newName, current(), &error)) {
        QMessageBox::warning(this, "Cannot save color profile", error);
        return false;
    }
    baseline = current();
    name = newName;
    setWindowTitle("Edit Color Profile: " + name);
    if (onSaved) onSaved();
    return true;
}
void ProfileEditorDialog::done(int) {
    blocks->commitEdit();
    phrases->commitEdit();
    if (current() == baseline) { QDialog::done(QDialog::Rejected); return; }
    const auto answer = QMessageBox::question(this, "Save color profile?", "Save the changes to this color profile?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return;
    if (answer == QMessageBox::Save && !saveProfile()) return;
    QDialog::done(QDialog::Rejected);
}

// ---- extension -> profile bindings ------------------------------------------------
ColorProfilesDialog::ColorProfilesDialog(const QVector<ProfileBinding> &bindings, const ProfileStore &profiles, ApplyFn applyFn,
                                         std::function<void()> changed, QWidget *parent)
    : QDialog(parent), store(profiles), apply(std::move(applyFn)), profilesChanged(std::move(changed)), saved(bindings) {
    setWindowTitle("Color Profiles");
    resize(580, 480);
    auto layout = new QVBoxLayout(this);
    auto help = new QLabel("Files with the extension in the left column are colored with the profile on the right. "
                           "Click an extension to edit it, or a profile to choose another one.", this);
    help->setWordWrap(true);
    layout->addWidget(help);
    table = new QTableWidget(0, 2, this);
    table->setObjectName("profileBindings");
    table->setHorizontalHeaderLabels({"File extension", "Color profile"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);   // editing starts from cellClicked below
    table->setItemDelegateForColumn(1, new ProfileComboDelegate(store, table));
    for (const ProfileBinding &binding : saved) {
        table->insertRow(table->rowCount());
        fillRow(table->rowCount() - 1, binding);
    }
    layout->addWidget(table, 1);
    connect(table, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (QTableWidgetItem *item = table->item(row, column)) table->editItem(item);
    });
    connect(table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (updating) return;
        const int row = item->row();
        QTimer::singleShot(0, this, [this, row] { rowEdited(row); });   // after the cell editor has closed
    });
    auto bindingButtons = new QHBoxLayout;
    auto add = new QPushButton("Add Rule", this);
    auto save = new QPushButton("Save", this);
    auto remove = new QPushButton("Remove", this);
    bindingButtons->addWidget(add);
    bindingButtons->addWidget(save);
    bindingButtons->addWidget(remove);
    bindingButtons->addStretch();
    layout->addLayout(bindingButtons);
    status = new QLabel(this);
    status->setWordWrap(true);
    layout->addWidget(status);
    layout->addWidget(separator(this));
    auto heading = new QLabel("Color profile files (" + QDir::toNativeSeparators(store.directory()) + ", *.prf)", this);
    QFont bold = heading->font();
    bold.setBold(true);
    heading->setFont(bold);
    heading->setWordWrap(true);
    layout->addWidget(heading);
    auto profileButtons = new QHBoxLayout;
    auto create = new QPushButton("Create New Color Profile", this);
    auto edit = new QPushButton("Edit Selected Profile...", this);
    auto close = new QPushButton("Close", this);
    profileButtons->addWidget(create);
    profileButtons->addWidget(edit);
    profileButtons->addStretch();
    profileButtons->addWidget(close);
    layout->addLayout(profileButtons);
    connect(add, &QPushButton::clicked, this, [this] {
        status->clear();
        if (table->rowCount() == saved.size()) {
            if (store.names().isEmpty()) status->setText("There are no color profiles yet. Use \"Create New Color Profile\" first.");
            table->insertRow(table->rowCount());
            fillRow(table->rowCount() - 1, {QString(), store.names().value(0)});
        }
        const int row = table->rowCount() - 1;
        table->setCurrentCell(row, 0);
        table->editItem(table->item(row, 0));
    });
    connect(save, &QPushButton::clicked, this, [this] { saveNewRow(); });
    connect(remove, &QPushButton::clicked, this, [this] { removeSelected(); });
    connect(create, &QPushButton::clicked, this, [this] { openEditor(QString()); });
    connect(edit, &QPushButton::clicked, this, [this] {
        const int row = table->currentRow();
        if (row < 0) { status->setText("Select a row first."); return; }
        openEditor(table->item(row, 1)->text());
    });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
}
void ColorProfilesDialog::fillRow(int row, const ProfileBinding &binding) {
    updating = true;
    table->setItem(row, 0, new QTableWidgetItem(binding.extension));
    table->setItem(row, 1, new QTableWidgetItem(binding.profile));
    updating = false;
}
bool ColorProfilesDialog::validateBinding(int row, const ProfileBinding &binding, QString *problem) const {
    if (binding.extension.isEmpty()) { *problem = "Enter a file extension without spaces or path characters, for example: cpp"; return false; }
    if (binding.profile.isEmpty()) { *problem = "Choose a color profile."; return false; }
    bool exists = false;
    for (const QString &name : store.names()) exists = exists || name.compare(binding.profile, Qt::CaseInsensitive) == 0;
    if (!exists) { *problem = "The color profile \"" + binding.profile + "\" does not exist in " + QDir::toNativeSeparators(store.directory()); return false; }
    for (int other = 0; other < table->rowCount(); ++other) {
        if (other == row) continue;
        if (ProfileStore::normalizeExtension(table->item(other, 0)->text()) == binding.extension) {
            *problem = "The extension \"" + binding.extension + "\" is already bound to \"" + table->item(other, 1)->text() + "\".";
            return false;
        }
    }
    return true;
}
void ColorProfilesDialog::rowEdited(int row) {
    if (row < 0 || row >= table->rowCount() || row >= saved.size()) return;     // new rows wait for "Save"
    const ProfileBinding before = saved[row];
    const ProfileBinding now{ProfileStore::normalizeExtension(table->item(row, 0)->text()), table->item(row, 1)->text().trimmed()};
    if (now == before) { fillRow(row, before); return; }
    QString problem;
    if (!validateBinding(row, now, &problem)) {
        QMessageBox::warning(this, "Color Profiles", problem);
        fillRow(row, before);
        return;
    }
    const QString text = QString("Save this change?\n\nBefore:  %1  ->  %2\nAfter:    %3  ->  %4")
        .arg(before.extension, before.profile, now.extension, now.profile);
    QMessageBox box(QMessageBox::Question, "Save changes?", text, QMessageBox::Save | QMessageBox::Discard, this);
    box.setDefaultButton(QMessageBox::Save);
    box.setTextFormat(Qt::PlainText);
    if (box.exec() != QMessageBox::Save) { fillRow(row, before); return; }    // discard: back to the previous values
    QVector<ProfileBinding> candidate = saved;
    candidate[row] = now;
    if (apply(candidate)) { saved = candidate; fillRow(row, now); status->setText("Saved."); }
    else fillRow(row, before);
}
void ColorProfilesDialog::saveNewRow() {
    if (table->rowCount() <= saved.size()) {
        status->setText("Nothing to save. Add a rule first; changes to existing rows are saved when you confirm them.");
        return;
    }
    const int row = table->rowCount() - 1;
    const ProfileBinding now{ProfileStore::normalizeExtension(table->item(row, 0)->text()), table->item(row, 1)->text().trimmed()};
    QString problem;
    if (!validateBinding(row, now, &problem)) { QMessageBox::warning(this, "Color Profiles", problem); return; }
    QVector<ProfileBinding> candidate = saved;
    candidate.append(now);
    if (!apply(candidate)) return;
    saved = candidate;
    fillRow(row, now);
    status->setText("Saved.");
}
void ColorProfilesDialog::removeSelected() {
    const int row = table->currentRow();
    if (row < 0 || row >= table->rowCount()) { status->setText("Select the row to remove first."); return; }
    if (row < saved.size()) {
        QVector<ProfileBinding> candidate = saved;
        candidate.removeAt(row);
        if (!apply(candidate)) return;
        saved = candidate;
    }
    table->removeRow(row);
    status->setText("Removed. Files with that extension are no longer colored by a profile.");
}
void ColorProfilesDialog::openEditor(const QString &profileName) {
    ColorProfile profile;
    if (!profileName.isEmpty()) {
        QStringList warnings;
        if (!store.load(profileName, &profile, &warnings)) {
            QMessageBox::warning(this, "Cannot open color profile", warnings.join('\n'));
            return;
        }
    }
    ProfileEditorDialog editor(store, profileName, profile, profilesChanged, this);
    editor.exec();
}
