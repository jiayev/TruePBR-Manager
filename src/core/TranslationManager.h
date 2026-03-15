#pragma once

#include <QApplication>
#include <QDateTime>
#include <QHash>
#include <QTimer>
#include <QTranslator>

#include <vector>

namespace tpbr
{

/// A QTranslator that reads translations from a JSON file.
/// JSON schema:
/// {
///   "locale": "zh_CN",
///   "name": "简体中文",
///   "translations": {
///     "ClassName": { "source text": "translated text", ... },
///     ...
///   }
/// }
class JsonTranslator : public QTranslator
{
    Q_OBJECT

  public:
    explicit JsonTranslator(QObject* parent = nullptr);

    bool loadFromFile(const QString& path);

    QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr,
                      int n = -1) const override;
    bool isEmpty() const override;

    QString locale() const
    {
        return m_locale;
    }
    QString languageName() const
    {
        return m_name;
    }

  private:
    // context -> (sourceText -> translation)
    QHash<QString, QHash<QString, QString>> m_data;
    QString m_locale;
    QString m_name;
};

// ────────────────────────────────────────────────────────────

struct LanguageInfo
{
    QString locale;
    QString name;
    QString filePath;
};

/// Singleton that manages available translations and the active translator.
/// Scans a `translations/` directory next to the executable (with a source-tree
/// fallback for development builds), watches it for changes, and provides
/// automatic locale selection with Chinese-family fallback.
class TranslationManager : public QObject
{
    Q_OBJECT

  public:
    static TranslationManager& instance();

    /// Scan for translation files, auto-select the best locale, and install
    /// a file-system watcher. Call once *before* creating any QWidgets.
    void init();

    /// All discovered languages (may change at runtime via hot-reload).
    const std::vector<LanguageInfo>& availableLanguages() const
    {
        return m_languages;
    }

    /// The locale tag of the currently active translation ("en" when none).
    QString currentLocale() const
    {
        return m_currentLocale;
    }

    /// Switch to a different locale.  Installs / removes the translator and
    /// causes Qt to send QEvent::LanguageChange to all top-level widgets.
    void switchLanguage(const QString& locale);

  signals:
    void languageChanged(const QString& locale);
    void availableLanguagesChanged();

  private:
    explicit TranslationManager(QObject* parent = nullptr);
    ~TranslationManager() override = default;
    TranslationManager(const TranslationManager&) = delete;
    TranslationManager& operator=(const TranslationManager&) = delete;

    /// (Re-)scan the translations directory and rebuild m_languages.
    void scanTranslations();

    /// Pick the best locale from m_languages for the current system locale.
    QString findBestLocale() const;

    /// Return the resolved translations directory path.
    QString translationsDir() const;

    /// Reload the translator for the current locale from disk (hot-reload).
    void reloadCurrent();

    /// Periodic check for file changes (replaces QFileSystemWatcher).
    void pollForChanges();

    /// Record current file timestamps for change detection.
    void snapshotTimestamps();

  private:
    std::vector<LanguageInfo> m_languages;
    QString m_currentLocale; // "en" when no translator is active
    JsonTranslator* m_translator = nullptr;
    QTimer m_pollTimer;
    QString m_translationsPath;
    QDateTime m_lastDirModified;               // last known directory modification time
    QMap<QString, QDateTime> m_fileTimestamps; // per-file last modification time
};

} // namespace tpbr
