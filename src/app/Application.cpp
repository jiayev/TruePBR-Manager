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
    // Release: console at info, file at info
    Log::init(spdlog::level::info, spdlog::level::info);
#else
    // Debug: console at debug, file at debug
    Log::init(spdlog::level::debug, spdlog::level::debug);
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
