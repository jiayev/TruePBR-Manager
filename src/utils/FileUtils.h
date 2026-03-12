#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace tpbr
{

class FileUtils
{
  public:
    /// Get file extension in lowercase (e.g. ".dds", ".png")
    static std::string getExtensionLower(const std::filesystem::path& path);

    /// Check if a file is a supported image format
    static bool isSupportedImage(const std::filesystem::path& path);

    /// List all supported image files in a directory
    static std::vector<std::filesystem::path> listImages(const std::filesystem::path& dir);
};

} // namespace tpbr
