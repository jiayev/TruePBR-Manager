#include "Log.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdlib>
#include <vector>

namespace tpbr
{

static std::filesystem::path s_logFilePath;

static std::filesystem::path getLogDirectory()
{
    // Use %APPDATA%/TruePBR-Manager/logs/ on Windows
    std::filesystem::path logDir;

#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata)
    {
        logDir = std::filesystem::path(appdata) / "TruePBR-Manager" / "logs";
    }
#endif

    // Fallback: logs/ next to the executable
    if (logDir.empty())
    {
        logDir = std::filesystem::current_path() / "logs";
    }

    return logDir;
}

void Log::init(spdlog::level::level_enum consoleLevel, spdlog::level::level_enum fileLevel)
{
    try
    {
        // Create log directory
        auto logDir = getLogDirectory();
        std::filesystem::create_directories(logDir);

        s_logFilePath = logDir / "truepbr.log";

        // Console sink (colored)
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_level(consoleLevel);
        consoleSink->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

        // Rotating file sink: 5 MB max, 3 backup files
        auto fileSink =
            std::make_shared<spdlog::sinks::rotating_file_sink_mt>(s_logFilePath.string(), 5 * 1024 * 1024, 3);
        fileSink->set_level(fileLevel);
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [tid %t] [%s:%#] %v");

        // Create multi-sink logger and set as default
        std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};
        auto logger = std::make_shared<spdlog::logger>("truepbr", sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::trace); // let sinks filter
        logger->flush_on(spdlog::level::warn);   // auto-flush on warn+

        spdlog::set_default_logger(logger);

        // Also flush periodically for debug messages
        spdlog::flush_every(std::chrono::seconds(3));

        SPDLOG_INFO("Logging initialized");
        SPDLOG_INFO("Log file: {}", s_logFilePath.string());
    }
    catch (const spdlog::spdlog_ex& ex)
    {
        // If file logging fails, at least set up console
        spdlog::set_level(consoleLevel);
        spdlog::error("Log init failed: {}. Falling back to console only.", ex.what());
    }
}

void Log::shutdown()
{
    spdlog::info("Logging shutdown");
    spdlog::shutdown();
}

std::filesystem::path Log::logFilePath()
{
    return s_logFilePath;
}

} // namespace tpbr
