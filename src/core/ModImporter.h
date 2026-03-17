#pragma once

#include "Project.h"

#include <filesystem>
#include <string>
#include <vector>

namespace tpbr
{

/// Diagnostic message produced during mod import.
struct ImportDiagnostic
{
    enum class Severity
    {
        Info,
        Warning,
        Error,
    };

    Severity severity = Severity::Info;
    std::string message;
};

/// Result of importing a PBR mod directory.
struct ModImportResult
{
    bool success = false;
    Project project;
    std::vector<ImportDiagnostic> diagnostics;
};

/// Result of scanning a mod directory for PGPatcher JSON files.
struct ModScanResult
{
    bool success = false;
    std::filesystem::path patcherDir;                     ///< Resolved PBRNIFPatcher directory
    std::vector<std::filesystem::path> jsonFiles;         ///< All JSON files found (sorted, relative display-friendly)
    std::vector<std::filesystem::path> jsonFilesAbsolute; ///< Corresponding absolute paths
    std::string errorMessage;                             ///< Non-empty if success == false
};

/// Imports an existing PBR mod directory into a TruePBR Manager project.
///
/// Reads PGPatcher JSON files from `<modDir>/PBRNIFPatcher/` (including
/// subdirectories) and resolves textures from `<modDir>/textures/pbr/`.
///
/// The JSON format follows the PGPatcher Mod Authors specification:
///   https://github.com/hakasapl/PGPatcher/wiki/Mod-Authors
///
/// Supported JSON layouts:
///   - Flat array:  [{entry}, {entry}, ...]
///   - With defaults: {"default": {...}, "entries": [{entry}, ...]}
class ModImporter
{
  public:
    /// Scan a mod directory for PGPatcher JSON files under PBRNIFPatcher/
    /// (recursively, including all subdirectories).
    /// Returns the list of found JSON files for the caller to present to the user.
    static ModScanResult scanForJsonFiles(const std::filesystem::path& modDir);

    /// Import a single PGPatcher JSON file from a mod directory.
    /// Call scanForJsonFiles() first, then let the user pick one, then call this.
    static ModImportResult importJsonFile(const std::filesystem::path& jsonPath, const std::filesystem::path& modDir);

    /// Parse a single PGPatcher JSON file and return the resulting texture sets.
    /// Textures are resolved relative to modDir.
    static std::vector<PBRTextureSet> parseJsonFile(const std::filesystem::path& jsonPath,
                                                    const std::filesystem::path& modDir,
                                                    std::vector<ImportDiagnostic>& diagnostics);
};

} // namespace tpbr
