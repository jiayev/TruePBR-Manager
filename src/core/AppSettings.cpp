#include "AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#ifdef _WIN32
#include <Windows.h>
#endif

#include <filesystem>

namespace tpbr
{

static constexpr int kMaxRecentProjects = 10;

static QString iniFilePath()
{
#ifdef _WIN32
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    auto dir = std::filesystem::path(exePath).parent_path();
#else
    auto dir = std::filesystem::path(QCoreApplication::applicationDirPath().toStdString());
#endif
    auto iniPath = dir / "TruePBR-Manager.ini";
    return QString::fromStdString(iniPath.string());
}

AppSettings& AppSettings::instance()
{
    static AppSettings inst;
    return inst;
}

AppSettings::AppSettings() : m_settings(iniFilePath(), QSettings::IniFormat) {}

QVariant AppSettings::value(const QString& key, const QVariant& defaultValue) const
{
    return m_settings.value(key, defaultValue);
}

void AppSettings::setValue(const QString& key, const QVariant& value)
{
    m_settings.setValue(key, value);
}

// ── Convenience accessors ───────────────────────────────────

QString AppSettings::language() const
{
    return m_settings.value("General/language").toString();
}

void AppSettings::setLanguage(const QString& locale)
{
    m_settings.setValue("General/language", locale);
}

QByteArray AppSettings::windowGeometry() const
{
    return m_settings.value("MainWindow/geometry").toByteArray();
}

void AppSettings::setWindowGeometry(const QByteArray& data)
{
    m_settings.setValue("MainWindow/geometry", data);
}

QByteArray AppSettings::windowState() const
{
    return m_settings.value("MainWindow/state").toByteArray();
}

void AppSettings::setWindowState(const QByteArray& data)
{
    m_settings.setValue("MainWindow/state", data);
}

QString AppSettings::lastExportPath() const
{
    return m_settings.value("Paths/lastExportPath").toString();
}

void AppSettings::setLastExportPath(const QString& path)
{
    m_settings.setValue("Paths/lastExportPath", path);
}

QString AppSettings::lastProjectDir() const
{
    return m_settings.value("Paths/lastProjectDir").toString();
}

void AppSettings::setLastProjectDir(const QString& dir)
{
    m_settings.setValue("Paths/lastProjectDir", dir);
}

QStringList AppSettings::recentProjects() const
{
    return m_settings.value("RecentProjects/paths").toStringList();
}

void AppSettings::addRecentProject(const QString& filePath)
{
    QStringList list = recentProjects();
    list.removeAll(filePath);
    list.prepend(filePath);
    while (list.size() > kMaxRecentProjects)
        list.removeLast();
    m_settings.setValue("RecentProjects/paths", list);
}

void AppSettings::removeRecentProject(const QString& filePath)
{
    QStringList list = recentProjects();
    list.removeAll(filePath);
    m_settings.setValue("RecentProjects/paths", list);
}

} // namespace tpbr
