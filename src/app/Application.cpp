#include "Application.h"
#include "MainWindow.h"
#include "core/AppSettings.h"
#include "core/TranslationManager.h"
#include "utils/FileAssociation.h"
#include "utils/Log.h"
#include "Version.h"

#include <QFileInfo>
#include <QMessageBox>

namespace tpbr
{

Application::Application(int& argc, char** argv) : m_app(argc, argv)
{
    m_app.setApplicationName("TruePBR Manager");
    m_app.setApplicationVersion(TRUEPBR_VERSION_STRING);
    m_app.setOrganizationName("TruePBR");

    initLogging();
    initStyle();

    // Check command-line arguments for a .tpbr file path
    const QStringList args = m_app.arguments();
    if (args.size() > 1)
    {
        const QString candidate = args.at(1);
        if (candidate.endsWith(QStringLiteral(".tpbr"), Qt::CaseInsensitive) && QFileInfo::exists(candidate))
        {
            m_startupProjectPath = candidate;
            SPDLOG_INFO("Startup project from command-line: {}", candidate.toStdString());
        }
    }
}

Application::~Application()
{
    Log::shutdown();
}

void Application::initLogging()
{
#ifdef NDEBUG
    // Release: normal logging – console & file at info level
    Log::init(spdlog::level::info, spdlog::level::info);
#else
    // Debug: advanced logging – console at debug, file at trace
    Log::init(spdlog::level::debug, spdlog::level::trace);
#endif

    SPDLOG_INFO("TruePBR Manager v{} ({}  {})", TRUEPBR_VERSION_STRING, TRUEPBR_GIT_HASH, TRUEPBR_BUILD_TIMESTAMP);
}

void Application::initStyle()
{
    m_app.setStyle("Fusion");
}

void Application::checkFirstLaunchAssociation()
{
    const bool alreadyAsked = AppSettings::instance().value("General/fileAssocRegistered", false).toBool();
    if (alreadyAsked)
        return;

    // Mark as asked regardless of the user's choice (don't ask again)
    AppSettings::instance().setValue("General/fileAssocRegistered", true);

    // If already registered (e.g. by an installer), skip the prompt
    if (FileAssociation::isRegistered())
        return;

    auto result =
        QMessageBox::question(nullptr, QObject::tr("File Association"),
                              QObject::tr("Would you like to register .tpbr files to open with TruePBR Manager?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

    if (result == QMessageBox::Yes)
    {
        FileAssociation::registerAssociation();
    }
}

int Application::run()
{
    TranslationManager::instance().init();

    MainWindow window;
    window.show();

    // Open a project from command-line argument (e.g. double-click .tpbr in Explorer)
    if (!m_startupProjectPath.isEmpty())
    {
        window.openProjectFromPath(m_startupProjectPath);
    }

    // On first launch, offer to register the file association
    checkFirstLaunchAssociation();

    return m_app.exec();
}

} // namespace tpbr
