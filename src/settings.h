#pragma once
#include <QColor>
#include <QFont>
#include <QStringList>
#include <QVector>

struct ColorRule {
    QString text;
    QColor color;
    bool operator==(const ColorRule &other) const { return text == other.text && color == other.color; }
};
struct ThemeColors {
    QColor editor, window, selection;
    // Toolbar toggle buttons (Blocks Color, Phrases Color, Find Color): default and pressed state.
    QColor buttonOff, buttonOn;
    bool operator==(const ThemeColors &other) const {
        return editor == other.editor && window == other.window && selection == other.selection
            && buttonOff == other.buttonOff && buttonOn == other.buttonOn;
    }
};

// A color profile (*.prf in the Profiles folder): the Blocks and Phrases rules for one kind of file.
struct ColorProfile {
    QVector<ColorRule> blocks, phrases;
    bool operator==(const ColorProfile &other) const { return blocks == other.blocks && phrases == other.phrases; }
};
// "Files with this extension use that profile." The extension is stored lowercase without a dot.
struct ProfileBinding {
    QString extension, profile;
    bool operator==(const ProfileBinding &other) const { return extension == other.extension && profile == other.profile; }
};

struct AppSettings {
    // Files are colored by the profile bound to their extension; other files get no Blocks/Phrases coloring.
    QVector<ProfileBinding> bindings;
    QColor findColor = Qt::red;
    QFont editorFont;
    bool blocksEnabled = true, phrasesEnabled = true, findEnabled = true;
    bool night = false;
    ThemeColors dayColors{QColor("#ffffff"), QColor("#f2f3f5"), QColor("#315fb4"), QColor("#e1e4e8"), QColor("#7aa7e8")};
    ThemeColors nightColors{QColor("#191c21"), QColor("#292d33"), QColor("#315fb4"), QColor("#3b414a"), QColor("#3b7be0")};
    QString wrapMode = "automatic";
    QByteArray geometry, splitter;
    AppSettings();
};

// An explicit directory also allows tests to avoid the user's real settings.
class SettingsStore {
public:
    explicit SettingsStore(const QString &directory = QString());
    AppSettings load(QStringList *warnings);
    bool save(const AppSettings &settings, QString *error) const;
    QString directory() const { return configDirectory; }
    QString fileName() const;
private:
    QString configDirectory;
    bool writable = true;
};

// Profiles live in <config directory>/Profiles/<name>.prf, next to settings.ini and op_doc.ini.
class ProfileStore {
public:
    explicit ProfileStore(const QString &configDirectory) : configDirectory(configDirectory) {}
    QString directory() const;
    QString pathFor(const QString &name) const;
    QStringList names() const;                       // profile names, sorted, without the .prf suffix
    bool load(const QString &name, ColorProfile *profile, QStringList *warnings) const;
    bool save(const QString &name, const ColorProfile &profile, QString *error) const;
    static bool validName(const QString &name, QString *reason = nullptr);
    // "*.CPP", ".cpp" and " cpp " all become "cpp"; an invalid extension becomes an empty string.
    static QString normalizeExtension(const QString &text);
private:
    QString configDirectory;
};

struct SessionState { QStringList files; QString active; };
class SessionStore {
public:
    explicit SessionStore(const QString &directory) : configDirectory(directory) {}
    SessionState load(QStringList *warnings);
    bool save(const SessionState &state, QString *error) const;
    QString fileName() const;
private:
    QString configDirectory;
    bool writable = true;
};
