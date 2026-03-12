#include "FileUtils.h"

#include <algorithm>

namespace tpbr
{

std::string FileUtils::getExtensionLower(const std::filesystem::path& path)
{
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

bool FileUtils::isSupportedImage(const std::filesystem::path& path)
{
    auto ext = getExtensionLower(path);
    return ext == ".png" || ext == ".dds" || ext == ".tga" || ext == ".bmp" || ext == ".jpg" || ext == ".jpeg";
}

std::vector<std::filesystem::path> FileUtils::listImages(const std::filesystem::path& dir)
{
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::is_directory(dir))
        return result;

    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (entry.is_regular_file() && isSupportedImage(entry.path()))
        {
            result.push_back(entry.path());
        }
    }
    return result;
}

} // namespace tpbr
