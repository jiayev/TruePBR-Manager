#include "TranslationManager.h"
#include "AppSettings.h"
#include "utils/Log.h"

#include <nlohmann/json.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>

#include <filesystem>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace tpbr
{

// ─── JsonTranslator ────────────────────────────────────────

JsonTranslator::JsonTranslator(QObject* parent) : QTranslator(parent) {}

bool JsonTranslator::loadFromFile(const QString& path)
{
    m_data.clear();
    m_locale.clear();
    m_name.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        SPDLOG_WARN("JsonTranslator: cannot open {}", path.toStdString());
        return false;
    }

    try
    {
        auto doc = nlohmann::json::parse(file.readAll().toStdString());

        m_locale = QString::fromStdString(doc.value("locale", ""));
        m_name = QString::fromStdString(doc.value("name", ""));

        if (doc.contains("translations") && doc["translations"].is_object())
        {
            for (auto& [ctx, table] : doc["translations"].items())
            {
                if (!table.is_object())
                    continue;
                auto ctxKey = QString::fromStdString(ctx);
                auto& bucket = m_data[ctxKey];
                for (auto& [src, trans] : table.items())
                {
                    if (trans.is_string())
                    {
                        bucket.insert(QString::fromStdString(src), QString::fromStdString(trans.get<std::string>()));
                    }
                }
            }
        }

        SPDLOG_INFO("JsonTranslator: loaded {} ({}) from {}", m_locale.toStdString(), m_name.toStdString(),
                    path.toStdString());
        return true;
    }
    catch (const nlohmann::json::exception& e)
    {
        SPDLOG_ERROR("JsonTranslator: JSON parse error in {}: {}", path.toStdString(), e.what());
        return false;
    }
}

QString JsonTranslator::translate(const char* context, const char* sourceText, const char* /*disambiguation*/,
                                  int /*n*/) const
{
    auto ctxIt = m_data.find(QString::fromUtf8(context));
    if (ctxIt == m_data.end())
        return {};
    auto srcIt = ctxIt->find(QString::fromUtf8(sourceText));
    if (srcIt == ctxIt->end())
        return {};
    return *srcIt;
}

bool JsonTranslator::isEmpty() const
{
    return m_data.isEmpty();
}

// ─── TranslationManager ───────────────────────────────────

TranslationManager& TranslationManager::instance()
{
    // Owned by QApplication so it is destroyed BEFORE QApplication is torn down,
    // avoiding dangling QFileSystemWatcher / QTranslator use after the event
    // system is gone.
    static TranslationManager* inst = nullptr;
    if (!inst)
    {
        inst = new TranslationManager(qApp);
        // Reset the pointer when the object is destroyed (app shutdown).
        QObject::connect(inst, &QObject::destroyed, [] { inst = nullptr; });
    }
    return *inst;
}

TranslationManager::TranslationManager(QObject* parent) : QObject(parent) {}

QString TranslationManager::translationsDir() const
{
    return m_translationsPath;
}

void TranslationManager::init()
{
    // Determine translations directory ── same strategy as HDRI loading:
    // 1. Next to the executable  (distribution)
    // 2. In the source tree       (development)
#ifdef _WIN32
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    auto exeDir = std::filesystem::path(exePath).parent_path();
#else
    auto exeDir = std::filesystem::path(QCoreApplication::applicationDirPath().toStdString());
#endif
    auto distDir = exeDir / "translations";
    auto srcDir = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "translations";

    if (std::filesystem::is_directory(distDir))
        m_translationsPath = QString::fromStdString(distDir.string());
    else if (std::filesystem::is_directory(srcDir))
        m_translationsPath = QString::fromStdString(srcDir.string());
    else
    {
        SPDLOG_WARN("TranslationManager: no translations directory found");
        m_currentLocale = QStringLiteral("en");
        return;
    }

    SPDLOG_INFO("TranslationManager: using translations dir {}", m_translationsPath.toStdString());

    scanTranslations();

    // Restore saved locale, or auto-detect
    QString saved = AppSettings::instance().language();
    QString best = saved.isEmpty() ? findBestLocale() : saved;
    SPDLOG_INFO("TranslationManager: best locale is {}", best.toStdString());
    switchLanguage(best);

    // Record initial timestamps so the poll timer only reacts to real changes.
    snapshotTimestamps();

    // Start periodic polling (every 2 seconds) for hot-reload.
    connect(&m_pollTimer, &QTimer::timeout, this, &TranslationManager::pollForChanges);
    m_pollTimer.start(2000);
}

void TranslationManager::snapshotTimestamps()
{
    m_fileTimestamps.clear();
    QDir dir(m_translationsPath);
    m_lastDirModified = QFileInfo(m_translationsPath).lastModified();
    for (const auto& fi : dir.entryInfoList(QStringList() << "*.json", QDir::Files))
        m_fileTimestamps[fi.absoluteFilePath()] = fi.lastModified();
}

void TranslationManager::pollForChanges()
{
    QDir dir(m_translationsPath);
    if (!dir.exists())
        return;

    QDateTime dirMod = QFileInfo(m_translationsPath).lastModified();
    bool dirChanged = (dirMod != m_lastDirModified);

    // Check individual file timestamps
    QString changedFile;
    auto entries = dir.entryInfoList(QStringList() << "*.json", QDir::Files);
    for (const auto& fi : entries)
    {
        auto it = m_fileTimestamps.find(fi.absoluteFilePath());
        if (it == m_fileTimestamps.end() || it.value() != fi.lastModified())
        {
            changedFile = fi.absoluteFilePath();
            break;
        }
    }

    // Also detect removed files
    if (!dirChanged && changedFile.isEmpty())
    {
        if (entries.size() != m_fileTimestamps.size())
            dirChanged = true;
    }

    if (!dirChanged && changedFile.isEmpty())
        return;

    // Something changed — re-scan
    scanTranslations();
    snapshotTimestamps();
    emit availableLanguagesChanged();

    if (dirChanged)
    {
        // Check if the active locale's file was removed
        if (m_currentLocale.compare("en", Qt::CaseInsensitive) != 0)
        {
            bool found = false;
            for (const auto& lang : m_languages)
            {
                if (lang.locale.compare(m_currentLocale, Qt::CaseInsensitive) == 0)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                SPDLOG_INFO("TranslationManager: active locale file removed, switching to English");
                switchLanguage(QStringLiteral("en"));
                return;
            }
        }
    }

    if (!changedFile.isEmpty())
    {
        // If the changed file is the active translation, hot-reload it
        for (const auto& lang : m_languages)
        {
            if (lang.filePath == changedFile && lang.locale.compare(m_currentLocale, Qt::CaseInsensitive) == 0)
            {
                reloadCurrent();
                break;
            }
        }
    }
}

void TranslationManager::scanTranslations()
{
    m_languages.clear();

    QDir dir(m_translationsPath);
    if (!dir.exists())
        return;

    auto entries = dir.entryInfoList(QStringList() << "*.json", QDir::Files);
    for (const auto& fi : entries)
    {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        try
        {
            auto doc = nlohmann::json::parse(f.readAll().toStdString());
            LanguageInfo info;
            info.locale = QString::fromStdString(doc.value("locale", fi.baseName().toStdString()));
            info.name = QString::fromStdString(doc.value("name", info.locale.toStdString()));
            info.filePath = fi.absoluteFilePath();
            m_languages.push_back(std::move(info));
        }
        catch (const nlohmann::json::exception&)
        {
            // Skip malformed files
        }
    }
}

QString TranslationManager::findBestLocale() const
{
    if (m_languages.empty())
        return QStringLiteral("en");

    QString sysLocale = QLocale::system().name();   // e.g. "zh_CN", "en_US", "fr_FR"
    QString sysLang = sysLocale.section('_', 0, 0); // e.g. "zh", "en", "fr"

    // 1. Exact match
    for (const auto& lang : m_languages)
    {
        if (lang.locale.compare(sysLocale, Qt::CaseInsensitive) == 0)
            return lang.locale;
    }

    // 2. Chinese family special rule: zh_TW / zh_HK / zh_* prefer zh_CN before English
    if (sysLang.compare("zh", Qt::CaseInsensitive) == 0)
    {
        for (const auto& lang : m_languages)
        {
            if (lang.locale.compare("zh_CN", Qt::CaseInsensitive) == 0)
                return lang.locale;
        }
        // Any other Chinese variant
        for (const auto& lang : m_languages)
        {
            if (lang.locale.startsWith("zh", Qt::CaseInsensitive))
                return lang.locale;
        }
    }

    // 3. Language-only match (strip country code)
    for (const auto& lang : m_languages)
    {
        QString langTag = lang.locale.section('_', 0, 0);
        if (langTag.compare(sysLang, Qt::CaseInsensitive) == 0)
            return lang.locale;
    }

    // 4. Final fallback: English (no translator needed)
    return QStringLiteral("en");
}

void TranslationManager::switchLanguage(const QString& locale)
{
    // Remove existing translator
    if (m_translator)
    {
        QApplication::removeTranslator(m_translator);
        delete m_translator;
        m_translator = nullptr;
    }

    m_currentLocale = locale;

    // "en" means use source strings directly — no translator needed
    if (locale.compare("en", Qt::CaseInsensitive) == 0)
    {
        SPDLOG_INFO("TranslationManager: switched to English (source strings)");
        AppSettings::instance().setLanguage(m_currentLocale);
        emit languageChanged(m_currentLocale);
        return;
    }

    // Find the LanguageInfo for this locale
    const LanguageInfo* found = nullptr;
    for (const auto& lang : m_languages)
    {
        if (lang.locale.compare(locale, Qt::CaseInsensitive) == 0)
        {
            found = &lang;
            break;
        }
    }

    if (!found)
    {
        SPDLOG_WARN("TranslationManager: locale '{}' not found, falling back to English", locale.toStdString());
        m_currentLocale = QStringLiteral("en");
        AppSettings::instance().setLanguage(m_currentLocale);
        emit languageChanged(m_currentLocale);
        return;
    }

    m_translator = new JsonTranslator(this);
    if (m_translator->loadFromFile(found->filePath))
    {
        QApplication::installTranslator(m_translator);
    }
    else
    {
        SPDLOG_WARN("TranslationManager: failed to load {}, falling back to English", found->filePath.toStdString());
        delete m_translator;
        m_translator = nullptr;
        m_currentLocale = QStringLiteral("en");
    }

    AppSettings::instance().setLanguage(m_currentLocale);
    emit languageChanged(m_currentLocale);
}

void TranslationManager::reloadCurrent()
{
    if (!m_translator || m_currentLocale.compare("en", Qt::CaseInsensitive) == 0)
        return;

    const LanguageInfo* found = nullptr;
    for (const auto& lang : m_languages)
    {
        if (lang.locale.compare(m_currentLocale, Qt::CaseInsensitive) == 0)
        {
            found = &lang;
            break;
        }
    }

    if (!found)
        return;

    SPDLOG_INFO("TranslationManager: hot-reloading {}", found->filePath.toStdString());

    // Temporarily remove, reload, re-install
    QApplication::removeTranslator(m_translator);
    m_translator->loadFromFile(found->filePath);
    QApplication::installTranslator(m_translator);
    // installTranslator triggers QEvent::LanguageChange on all top-level widgets
}

} // namespace tpbr
