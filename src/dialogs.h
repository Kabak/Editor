#pragma once
#include "settings.h"
#include <QAbstractTableModel>
#include <QDialog>
#include <QIcon>
#include <functional>
#include <QHBoxLayout>

QIcon colorIcon(const QColor &color);
class RuleTableModel : public QAbstractTableModel {
public:
    explicit RuleTableModel(const QVector<ColorRule> &rules, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    void add();
    void remove(int row);
    QVector<ColorRule> rules() const { return values; }
private:
    QVector<ColorRule> values;
};

// Filter + rule table + Add/Remove buttons; used by the rules dialog and the profile editor.
class RulesEditor : public QWidget {
public:
    RulesEditor(const QVector<ColorRule> &rules, bool blocks, QWidget *parent);
    QVector<ColorRule> rules() const { return model->rules(); }
    QHBoxLayout *buttonRow() const { return row; }   // the owner may append buttons here
    void commitEdit();                               // finish an open cell editor
    bool validate();                                 // warns and returns false on empty/multi-line rules
private:
    RuleTableModel *model;
    QHBoxLayout *row = nullptr;
};

class RulesDialog : public QDialog {
public:
    RulesDialog(const QString &title, const QVector<ColorRule> &rules, bool blocks, QWidget *parent);
    QVector<ColorRule> rules() const;
protected:
    void done(int result) override;
private:
    RulesEditor *editor;
    QVector<ColorRule> original;
};
// Creates or edits one color profile (*.prf): Blocks and Phrases rules in two tabs.
class ProfileEditorDialog : public QDialog {
public:
    // name is empty for a new profile. onSaved runs after each successful save.
    ProfileEditorDialog(const ProfileStore &store, const QString &name, const ColorProfile &profile,
                        std::function<void()> onSaved, QWidget *parent);
protected:
    void done(int result) override;
private:
    ColorProfile current() const;
    bool saveProfile();
    const ProfileStore &store;
    QString name;
    RulesEditor *blocks, *phrases;
    ColorProfile baseline;
    std::function<void()> onSaved;
};
// Extension -> color profile bindings. Changes to saved rows are confirmed one by one;
// new rows are added with "Add Rule" + "Save". Every confirmed change is applied at once.
class ColorProfilesDialog : public QDialog {
public:
    using ApplyFn = std::function<bool(const QVector<ProfileBinding> &)>;
    ColorProfilesDialog(const QVector<ProfileBinding> &bindings, const ProfileStore &store, ApplyFn apply,
                        std::function<void()> profilesChanged, QWidget *parent);
private:
    void fillRow(int row, const ProfileBinding &binding);
    void rowEdited(int row);
    bool validateBinding(int row, const ProfileBinding &binding, QString *problem) const;
    void saveNewRow();
    void removeSelected();
    void openEditor(const QString &profileName);
    class QTableWidget *table = nullptr;
    class QLabel *status = nullptr;
    const ProfileStore &store;
    ApplyFn apply;
    std::function<void()> profilesChanged;
    QVector<ProfileBinding> saved;
    bool updating = false;
};
class FindColorDialog : public QDialog {
public:
    explicit FindColorDialog(const QColor &color, QWidget *parent);
    QColor color() const { return selected; }
protected:
    void done(int result) override;
private:
    QColor original, selected;
};
class ThemeColorsDialog : public QDialog {
public:
    explicit ThemeColorsDialog(const AppSettings &settings, QWidget *parent);
    ThemeColors dayColors() const { return selectedDay; }
    ThemeColors nightColors() const { return selectedNight; }
protected:
    void done(int result) override;
private:
    ThemeColors originalDay, originalNight, selectedDay, selectedNight;
};
