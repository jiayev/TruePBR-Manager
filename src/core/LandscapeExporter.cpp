#include "LandscapeExporter.h"
#include "utils/Log.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace tpbr
{

namespace fs = std::filesystem;
using json = nlohmann::json;

static float rounded3(float v)
{
    return std::round(v * 1000.0f) / 1000.0f;
}

std::string LandscapeExporter::serializeLandscapeJson(const PBRParameters& params)
{
    json j;
    j["roughnessScale"] = rounded3(params.roughnessScale);
    j["displacementScale"] = rounded3(params.displacementScale);
    j["specularLevel"] = rounded3(params.specularLevel);
    j["subsurfaceColor"] = {rounded3(params.subsurfaceColor[0]), rounded3(params.subsurfaceColor[1]),
                            rounded3(params.subsurfaceColor[2])};
    j["subsurfaceOpacity"] = rounded3(params.subsurfaceOpacity);
    return j.dump(4);
}

bool LandscapeExporter::exportLandscapeJsons(const Project& project)
{
    if (project.outputModFolder.empty())
    {
        spdlog::error("LandscapeExporter: no output mod folder set");
        return false;
    }

    bool allOk = true;
    int count = 0;

    for (const auto& ts : project.textureSets)
    {
        if (ts.landscapeEdids.empty())
            continue;

        auto jsonDir = project.outputModFolder / "PBRTextureSets";
        fs::create_directories(jsonDir);

        for (const auto& edid : ts.landscapeEdids)
        {
            if (edid.empty())
                continue;

            auto jsonPath = jsonDir / (edid + ".json");
            std::ofstream ofs(jsonPath);
            if (!ofs)
            {
                spdlog::error("LandscapeExporter: failed to write {}", jsonPath.string());
                allOk = false;
                continue;
            }

            ofs << serializeLandscapeJson(ts.params);
            spdlog::info("LandscapeExporter: exported {}", jsonPath.string());
            ++count;
        }
    }

    if (count > 0)
    {
        spdlog::info("LandscapeExporter: exported {} landscape JSON files", count);
    }

    return allOk;
}

} // namespace tpbr
