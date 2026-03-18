#include "VanillaConverter.h"

#include <cmath>
#include <algorithm>

namespace tpbr
{

VanillaConversionResult VanillaConverter::convert(const VanillaConversionInput& input)
{
    VanillaConversionResult result;

    // Stub implementation: validate and return minimal result
    if (!validateInput(input, result.diagnostics))
    {
        result.success = false;
        return result;
    }

    result.success = false;
    return result;
}

uint8_t VanillaConverter::shininessToRoughness(float shininess)
{
    // Stub: return default value
    return 0;
}

float VanillaConverter::srgbToLinear(uint8_t srgb)
{
    // Stub: return default value
    return 0.0f;
}

uint8_t VanillaConverter::linearToSrgb(float linear)
{
    // Stub: return default value
    return 0;
}

void VanillaConverter::applyGammaBrightness(uint8_t* rgba, int width, int height, float gamma, float brightness)
{
    // Stub: no operation
    (void)rgba;
    (void)width;
    (void)height;
    (void)gamma;
    (void)brightness;
}

std::vector<uint8_t> VanillaConverter::extractAlphaChannel(const uint8_t* rgba, int width, int height)
{
    // Stub: return empty vector
    (void)rgba;
    (void)width;
    (void)height;
    return std::vector<uint8_t>();
}

std::array<float, 3> VanillaConverter::averageCubemapColor(const std::filesystem::path& cubemapPath)
{
    // Stub: return neutral color
    (void)cubemapPath;
    return {1.0f, 1.0f, 1.0f};
}

bool VanillaConverter::validateInput(const VanillaConversionInput& input, std::vector<ImportDiagnostic>& diags)
{
    bool hasDiffuse = input.inputFiles.find(VanillaTextureType::Diffuse) != input.inputFiles.end();
    bool hasNormal = input.inputFiles.find(VanillaTextureType::Normal) != input.inputFiles.end();

    // Check required textures
    if (!hasDiffuse)
    {
        ImportDiagnostic diag;
        diag.severity = ImportDiagnostic::Severity::Error;
        diag.message = "Diffuse texture is required for conversion";
        diags.push_back(diag);
    }

    if (!hasNormal)
    {
        ImportDiagnostic diag;
        diag.severity = ImportDiagnostic::Severity::Error;
        diag.message = "Normal texture is required for conversion";
        diags.push_back(diag);
    }

    return hasDiffuse && hasNormal;
}

} // namespace tpbr
