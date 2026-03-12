#include "Application.h"
#include "MainWindow.h"

#include <spdlog/spdlog.h>

namespace tpbr {

Application::Application(int& argc, char** argv)
    : m_app(argc, argv)
{
    m_app.setApplicationName("TruePBR Manager");
    m_app.setApplicationVersion("0.1.0");
    m_app.setOrganizationName("TruePBR");

    initLogging();
    initStyle();
}

Application::~Application() = default;

void Application::initLogging()
{
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("TruePBR Manager v{}", m_app.applicationVersion().toStdString());
}

void Application::initStyle()
{
    // Use Fusion style for a clean, consistent look across Windows versions
    m_app.setStyle("Fusion");
}

int Application::run()
{
    MainWindow window;
    window.show();
    return m_app.exec();
}

} // namespace tpbr
