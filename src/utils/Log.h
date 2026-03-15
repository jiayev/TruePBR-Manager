#pragma once

/// Centralized logging for TruePBR Manager.
///
/// Usage:
///   #include "utils/Log.h"
///   spdlog::info("message {}", value);    // uses the global default logger
///   SPDLOG_DEBUG("debug with source location");
///
/// Initialization:
///   tpbr::Log::init();       // call once at startup (Application.cpp)
///   tpbr::Log::shutdown();   // call at exit

// SPDLOG_ACTIVE_LEVEL is set by CMake compile definitions per build config:
//   Debug   -> SPDLOG_LEVEL_TRACE (0)  – all macros active
//   Release -> SPDLOG_LEVEL_INFO  (2)  – TRACE/DEBUG macros stripped
#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif

#include <spdlog/spdlog.h>

#include <filesystem>
#include <string>

namespace tpbr
{

class Log
{
  public:
    /// Initialize the logging system.
    /// Creates a dual-sink logger: console (colored) + rotating file.
    /// File logs go to: %APPDATA%/TruePBR-Manager/logs/truepbr.log
    /// @param consoleLevel  Minimum level for console output (default: info)
    /// @param fileLevel     Minimum level for file output (default: debug)
    static void init(spdlog::level::level_enum consoleLevel = spdlog::level::info,
                     spdlog::level::level_enum fileLevel = spdlog::level::debug);

    /// Flush all loggers and shut down spdlog. Call before exit.
    static void shutdown();

    /// Get the log file path (available after init).
    static std::filesystem::path logFilePath();
};

} // namespace tpbr
