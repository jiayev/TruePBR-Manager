#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>

namespace tpbr
{

/// Persistent application-level settings (independent of any project).
/// Uses QSettings with INI format, stored next to the executable as
/// "TruePBR-Manager.ini".  Falls back to the registry-based location
/// if the exe directory is not writable.
class AppSettings
{
  public:
    static AppSettings& instance();

    /// Read a value.  Returns \a defaultValue if the key does not exist.
    QVariant value(const QString& key, const QVariant& defaultValue = {}) const;

    /// Write a value.
    void setValue(const QString& key, const QVariant& value);

    // ── Convenience accessors ───────────────────────────────

    QString language() const;
    void setLanguage(const QString& locale);

    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& data);

    QByteArray windowState() const;
    void setWindowState(const QByteArray& data);

    QString lastExportPath() const;
    void setLastExportPath(const QString& path);

    QString lastProjectDir() const;
    void setLastProjectDir(const QString& dir);

    /// Returns the recent projects list (most recent first, max 10).
    QStringList recentProjects() const;

    /// Add a path to the front of the recent projects list.
    /// Removes duplicates and trims to 10 entries.
    void addRecentProject(const QString& filePath);

    /// Remove a specific path from the recent projects list.
    void removeRecentProject(const QString& filePath);

  private:
    AppSettings();
    ~AppSettings() = default;
    AppSettings(const AppSettings&) = delete;
    AppSettings& operator=(const AppSettings&) = delete;

    QSettings m_settings;
};

} // namespace tpbr
