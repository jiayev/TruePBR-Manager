#include "VanillaConverter.h"

#include "utils/DDSUtils.h"
#include "utils/ImageUtils.h"

#include <algorithm>
#include <cmath>

namespace tpbr
{

// ─── sRGB ↔ Linear conversion ──────────────────────────────────

float VanillaConverter::srgbToLinear(uint8_t srgb)
{
    const float x = static_cast<float>(srgb) / 255.0f;
    if (x <= 0.04045f)
        return x / 12.92f;
    return std::pow((x + 0.055f) / 1.055f, 2.4f);
}

uint8_t VanillaConverter::linearToSrgb(float linear)
{
    linear = std::clamp(linear, 0.0f, 1.0f);
    float s;
    if (linear <= 0.0031308f)
        s = linear * 12.92f;
    else
        s = 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(std::clamp(s * 255.0f + 0.5f, 0.0f, 255.0f));
}

// ─── Shininess → Roughness ─────────────────────────────────────

uint8_t VanillaConverter::shininessToRoughness(float shininess)
{
    if (shininess <= 0.0f)
        return 255; // fully rough

    // Formula: roughness = pow(2 / (2 + shininess), 0.25)
    const float roughness = std::pow(2.0f / (2.0f + shininess), 0.25f);
    const float mapped = std::clamp(roughness * 255.0f + 0.5f, 0.0f, 255.0f);
    return static_cast<uint8_t>(mapped);
}

// ─── Gamma/Brightness adjustment ───────────────────────────────

void VanillaConverter::applyGammaBrightness(uint8_t* rgba, int width, int height, float gamma, float brightness)
{
    if (!rgba || width <= 0 || height <= 0)
        return;

    // Identity check — skip if nothing to do
    if (gamma == 1.0f && brightness == 0.0f)
        return;

    const int pixelCount = width * height;
    const float invGamma = 1.0f / gamma;

    for (int i = 0; i < pixelCount; ++i)
    {
        uint8_t* pixel = rgba + i * 4;

        // Process R, G, B channels (leave A untouched)
        for (int c = 0; c < 3; ++c)
        {
            // sRGB → linear
            float linear = srgbToLinear(pixel[c]);

            // Apply gamma curve: pow(linear, 1/gamma)
            if (invGamma != 1.0f && linear > 0.0f)
                linear = std::pow(linear, invGamma);

            // Add brightness in linear space
            linear += brightness;

            // linear → sRGB (clamp is inside linearToSrgb)
            pixel[c] = linearToSrgb(linear);
        }
        // pixel[3] (alpha) is untouched
    }
}

// ─── Extract Alpha Channel ─────────────────────────────────────

std::vector<uint8_t> VanillaConverter::extractAlphaChannel(const uint8_t* rgba, int width, int height)
{
    if (!rgba || width <= 0 || height <= 0)
        return {};

    const int pixelCount = width * height;
    std::vector<uint8_t> alpha(static_cast<size_t>(pixelCount));
    for (int i = 0; i < pixelCount; ++i)
        alpha[i] = rgba[i * 4 + 3];
    return alpha;
}

// ─── Helpers ───────────────────────────────────────────────────

/// Load a texture file (DDS or raster) into RGBA8 pixels.
static bool loadTextureFile(const std::filesystem::path& path, int& width, int& height, std::vector<uint8_t>& pixels)
{
    const auto ext = path.extension().string();
    if (ext == ".dds" || ext == ".DDS")
    {
        if (DDSUtils::loadDDS(path, width, height, pixels))
            return true;
    }

    auto imageData = ImageUtils::loadImage(path);
    if (imageData.width > 0 && imageData.height > 0 && !imageData.pixels.empty())
    {
        width = imageData.width;
        height = imageData.height;
        pixels = std::move(imageData.pixels);

        // Pad RGB to RGBA if needed
        if (imageData.channels == 3)
        {
            std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
            for (int i = 0; i < width * height; ++i)
            {
                rgba[i * 4 + 0] = pixels[i * 3 + 0];
                rgba[i * 4 + 1] = pixels[i * 3 + 1];
                rgba[i * 4 + 2] = pixels[i * 3 + 2];
                rgba[i * 4 + 3] = 255;
            }
            pixels = std::move(rgba);
        }
        return true;
    }
    return false;
}

// ─── Full conversion pipeline ──────────────────────────────────

VanillaConversionResult VanillaConverter::convert(const VanillaConversionInput& input, ProgressCallback progress)
{
    VanillaConversionResult result;

    if (!validateInput(input, result.diagnostics))
    {
        result.success = false;
        return result;
    }

    // Count total steps for progress reporting
    int totalSteps = 6; // load diffuse, load normal, process, save albedo, save normal, save RMAOS
    if (input.inputFiles.count(VanillaTextureType::Glow))
        totalSteps += 1;
    if (input.inputFiles.count(VanillaTextureType::Parallax))
        totalSteps += 1;
    if (input.inputFiles.count(VanillaTextureType::BackLight))
        totalSteps += 1;
    int step = 0;

    auto reportProgress = [&](const std::string& desc) -> bool
    {
        if (progress && !progress(step, totalSteps, desc))
        {
            result.success = false;
            ImportDiagnostic d;
            d.severity = ImportDiagnostic::Severity::Warning;
            d.message = "Conversion cancelled by user";
            result.diagnostics.push_back(d);
            return false;
        }
        ++step;
        return true;
    };

    namespace fs = std::filesystem;

    // Ensure output directory exists
    const fs::path outputDir = input.outputDir;
    if (!outputDir.empty())
        fs::create_directories(outputDir);

    const std::string baseName = input.textureSetName.empty() ? "converted" : input.textureSetName;

    // ── Load Diffuse (required) ────────────────────────────────
    if (!reportProgress("Loading Diffuse..."))
        return result;
    int diffW = 0, diffH = 0;
    std::vector<uint8_t> diffusePixels;
    {
        auto it = input.inputFiles.find(VanillaTextureType::Diffuse);
        if (it == input.inputFiles.end() || !loadTextureFile(it->second, diffW, diffH, diffusePixels))
        {
            ImportDiagnostic d;
            d.severity = ImportDiagnostic::Severity::Error;
            d.message = "Failed to load Diffuse texture";
            result.diagnostics.push_back(d);
            result.success = false;
            return result;
        }
    }

    // ── Load Normal (required) ─────────────────────────────────
    if (!reportProgress("Loading Normal..."))
        return result;
    int normW = 0, normH = 0;
    std::vector<uint8_t> normalPixels;
    {
        auto it = input.inputFiles.find(VanillaTextureType::Normal);
        if (it == input.inputFiles.end() || !loadTextureFile(it->second, normW, normH, normalPixels))
        {
            ImportDiagnostic d;
            d.severity = ImportDiagnostic::Severity::Error;
            d.message = "Failed to load Normal texture";
            result.diagnostics.push_back(d);
            result.success = false;
            return result;
        }
    }

    // ── Apply gamma/brightness to Diffuse → Albedo ─────────────
    if (!reportProgress("Processing Albedo..."))
        return result;
    {
        auto gb = input.params.getColorAdjustment(VanillaTextureType::Diffuse);
        if (gb.gamma != 1.0f || gb.brightness != 0.0f)
            applyGammaBrightness(diffusePixels.data(), diffW, diffH, gb.gamma, gb.brightness);
    }

    // ── Cubemap tint overlay on Albedo ──────────────────────────
    {
        auto envIt = input.inputFiles.find(VanillaTextureType::EnvMask);
        auto cubeIt = input.inputFiles.find(VanillaTextureType::Cubemap);
        if (envIt != input.inputFiles.end() && cubeIt != input.inputFiles.end())
        {
            int envW = 0, envH = 0;
            std::vector<uint8_t> envPixels;
            if (loadTextureFile(envIt->second, envW, envH, envPixels))
            {
                // Load cubemap pixels and apply gamma/brightness before averaging
                int cubeW = 0, cubeH = 0;
                std::vector<uint8_t> cubePixels;
                std::array<float, 3> avgColor = {1.0f, 1.0f, 1.0f};
                if (loadTextureFile(cubeIt->second, cubeW, cubeH, cubePixels))
                {
                    auto cubeGb = input.params.getColorAdjustment(VanillaTextureType::Cubemap);
                    if (cubeGb.gamma != 1.0f || cubeGb.brightness != 0.0f)
                        applyGammaBrightness(cubePixels.data(), cubeW, cubeH, cubeGb.gamma, cubeGb.brightness);

                    double sumR = 0.0, sumG = 0.0, sumB = 0.0;
                    const int cubeCount = cubeW * cubeH;
                    for (int j = 0; j < cubeCount; ++j)
                    {
                        sumR += srgbToLinear(cubePixels[j * 4 + 0]);
                        sumG += srgbToLinear(cubePixels[j * 4 + 1]);
                        sumB += srgbToLinear(cubePixels[j * 4 + 2]);
                    }
                    avgColor = {static_cast<float>(sumR / cubeCount), static_cast<float>(sumG / cubeCount),
                                static_cast<float>(sumB / cubeCount)};
                }

                if (envW == diffW && envH == diffH)
                {
                    const int count = diffW * diffH;
                    for (int i = 0; i < count; ++i)
                    {
                        float mask = static_cast<float>(envPixels[i * 4 + 0]) / 255.0f;
                        if (mask <= 0.0f)
                            continue;
                        for (int c = 0; c < 3; ++c)
                        {
                            float linear = srgbToLinear(diffusePixels[i * 4 + c]);
                            float blended = linear + (avgColor[c] - linear) * mask; // lerp(diffuse, cubemap, envMask)
                            diffusePixels[i * 4 + c] = linearToSrgb(blended);
                        }
                    }
                }
            }
        }
    }

    // ── Generate RMAOS ─────────────────────────────────────────
    std::vector<uint8_t> rmaosPixels(static_cast<size_t>(diffW) * diffH * 4);
    {
        const uint8_t roughness = shininessToRoughness(input.params.shininess);

        // Metallic roughness override
        const bool hasMetalRoughOverride = input.params.metallicRoughnessOverride.has_value();
        const uint8_t metalRoughness = hasMetalRoughOverride
                                           ? static_cast<uint8_t>(std::clamp(
                                                 input.params.metallicRoughnessOverride.value() * 255.0f, 0.0f, 255.0f))
                                           : roughness;

        // Load optional inputs
        int specW = 0, specH = 0;
        std::vector<uint8_t> specPixels;
        auto specIt = input.inputFiles.find(VanillaTextureType::Specular);
        bool hasSpec = specIt != input.inputFiles.end() && loadTextureFile(specIt->second, specW, specH, specPixels);

        int envW = 0, envH = 0;
        std::vector<uint8_t> envPixels;
        auto envIt = input.inputFiles.find(VanillaTextureType::EnvMask);
        bool hasEnv = envIt != input.inputFiles.end() && loadTextureFile(envIt->second, envW, envH, envPixels);

        bool useNormalAlpha = input.params.normalAlphaIsSpecular && !hasSpec;

        const int count = diffW * diffH;
        for (int i = 0; i < count; ++i)
        {
            // Metallic from EnvMask
            uint8_t metallic = 0;
            if (hasEnv && envW == diffW && envH == diffH)
                metallic = envPixels[i * 4 + 0];

            // Roughness — override for metallic areas
            if (metallic > 0 && hasMetalRoughOverride)
                rmaosPixels[i * 4 + 0] = metalRoughness;
            else
                rmaosPixels[i * 4 + 0] = roughness;

            rmaosPixels[i * 4 + 1] = metallic;

            rmaosPixels[i * 4 + 2] = 255; // AO = white

            // Specular
            if (hasSpec && specW == diffW && specH == diffH)
                rmaosPixels[i * 4 + 3] = specPixels[i * 4 + 0];
            else if (useNormalAlpha && normW == diffW && normH == diffH)
                rmaosPixels[i * 4 + 3] = normalPixels[i * 4 + 3];
            else
                rmaosPixels[i * 4 + 3] = 20; // ~0.08 baseline
        }
    }

    // ── Save output files ──────────────────────────────────────
    if (!reportProgress("Saving Albedo DDS..."))
        return result;

    // Helper to save RGBA pixels as BC7 DDS
    auto saveOutput = [&](const std::string& suffix, const uint8_t* pixels, int w, int h, PBRTextureSlot slot,
                          bool srgb) -> bool
    {
        fs::path outPath = outputDir / (baseName + suffix + ".dds");
        bool ok = srgb ? DDSUtils::saveDDS_BC7(outPath, w, h, pixels, true)
                       : DDSUtils::saveDDS_BC7(outPath, w, h, pixels, false);
        if (ok)
        {
            result.generatedFiles[slot] = outPath;
            TextureEntry entry;
            entry.sourcePath = outPath;
            entry.slot = slot;
            entry.width = w;
            entry.height = h;
            entry.channels = 4;
            entry.format = "dds";
            result.generatedSet.textures[slot] = entry;
        }
        else
        {
            ImportDiagnostic d;
            d.severity = ImportDiagnostic::Severity::Error;
            d.message = "Failed to save " + suffix + ".dds";
            result.diagnostics.push_back(d);
        }
        return ok;
    };

    bool allOk = true;

    // Albedo (sRGB)
    allOk &= saveOutput("", diffusePixels.data(), diffW, diffH, PBRTextureSlot::Diffuse, true);

    // Normal (linear) — save with BC5 for better quality
    if (!reportProgress("Saving Normal DDS..."))
        return result;
    {
        fs::path outPath = outputDir / (baseName + "_n.dds");
        bool ok = DDSUtils::saveDDS_BC5(outPath, normW, normH, normalPixels.data());
        if (ok)
        {
            result.generatedFiles[PBRTextureSlot::Normal] = outPath;
            TextureEntry entry;
            entry.sourcePath = outPath;
            entry.slot = PBRTextureSlot::Normal;
            entry.width = normW;
            entry.height = normH;
            entry.channels = 4;
            entry.format = "dds";
            result.generatedSet.textures[PBRTextureSlot::Normal] = entry;
        }
        else
        {
            ImportDiagnostic d;
            d.severity = ImportDiagnostic::Severity::Error;
            d.message = "Failed to save _n.dds";
            result.diagnostics.push_back(d);
            allOk = false;
        }
    }

    // RMAOS (linear)
    if (!reportProgress("Saving RMAOS DDS..."))
        return result;
    allOk &= saveOutput("_rmaos", rmaosPixels.data(), diffW, diffH, PBRTextureSlot::RMAOS, false);

    // ── Optional outputs ───────────────────────────────────────

    // Emissive (Glow)
    {
        auto it = input.inputFiles.find(VanillaTextureType::Glow);
        if (it != input.inputFiles.end())
        {
            if (!reportProgress("Saving Emissive DDS..."))
                return result;
            int w = 0, h = 0;
            std::vector<uint8_t> pixels;
            if (loadTextureFile(it->second, w, h, pixels))
            {
                auto gb = input.params.getColorAdjustment(VanillaTextureType::Glow);
                if (gb.gamma != 1.0f || gb.brightness != 0.0f)
                    applyGammaBrightness(pixels.data(), w, h, gb.gamma, gb.brightness);
                allOk &= saveOutput("_g", pixels.data(), w, h, PBRTextureSlot::Emissive, true);
                result.generatedSet.features.emissive = true;
            }
        }
    }

    // Displacement (Parallax)
    {
        auto it = input.inputFiles.find(VanillaTextureType::Parallax);
        if (it != input.inputFiles.end())
        {
            if (!reportProgress("Saving Displacement DDS..."))
                return result;
            int w = 0, h = 0;
            std::vector<uint8_t> pixels;
            if (loadTextureFile(it->second, w, h, pixels))
            {
                allOk &= saveOutput("_p", pixels.data(), w, h, PBRTextureSlot::Displacement, false);
                result.generatedSet.features.parallax = true;
            }
        }
    }

    // Subsurface (BackLight)
    {
        auto it = input.inputFiles.find(VanillaTextureType::BackLight);
        if (it != input.inputFiles.end())
        {
            if (!reportProgress("Saving Subsurface DDS..."))
                return result;
            int w = 0, h = 0;
            std::vector<uint8_t> pixels;
            if (loadTextureFile(it->second, w, h, pixels))
            {
                auto gb = input.params.getColorAdjustment(VanillaTextureType::BackLight);
                if (gb.gamma != 1.0f || gb.brightness != 0.0f)
                    applyGammaBrightness(pixels.data(), w, h, gb.gamma, gb.brightness);
                allOk &= saveOutput("_s", pixels.data(), w, h, PBRTextureSlot::Subsurface, true);
                result.generatedSet.features.subsurface = true;
            }
        }
    }

    // ── Populate result ────────────────────────────────────────
    result.generatedSet.name = baseName;
    result.generatedSet.matchTexture = input.vanillaMatchPath.empty() ? baseName : input.vanillaMatchPath;
    result.generatedSet.rmaosSourceMode = RMAOSSourceMode::PackedTexture;

    result.success = allOk;
    return result;
}

// ─── Input validation ──────────────────────────────────────────

bool VanillaConverter::validateInput(const VanillaConversionInput& input, std::vector<ImportDiagnostic>& diags)
{
    bool hasDiffuse = input.inputFiles.find(VanillaTextureType::Diffuse) != input.inputFiles.end();
    bool hasNormal = input.inputFiles.find(VanillaTextureType::Normal) != input.inputFiles.end();

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
