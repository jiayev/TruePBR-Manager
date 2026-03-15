#include "Application.h"
#include "MainWindow.h"
#include "utils/Log.h"

namespace tpbr
{

Application::Application(int& argc, char** argv) : m_app(argc, argv)
{
    m_app.setApplicationName("TruePBR Manager");
    m_app.setApplicationVersion("0.1.0");
    m_app.setOrganizationName("TruePBR");

    initLogging();
    initStyle();
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

    SPDLOG_INFO("TruePBR Manager v{}", m_app.applicationVersion().toStdString());
}

void Application::initStyle()
{
    m_app.setStyle("Fusion");
}

int Application::run()
{
    MainWindow window;
    window.show();
    return m_app.exec();
}

} // namespace tpbr
