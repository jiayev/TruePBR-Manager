#include "TextureSetValidator.h"

#include <format>

namespace tpbr
{

static bool isPowerOfTwo(int n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

std::vector<ValidationIssue> TextureSetValidator::validate(const PBRTextureSet& ts)
{
    std::vector<ValidationIssue> issues;
    checkMatchTexture(ts, issues);
    checkRequiredSlots(ts, issues);
    checkResolutionConsistency(ts, issues);
    checkPowerOfTwo(ts, issues);
    checkFeatureTextures(ts, issues);
    checkSlotConflicts(ts, issues);
    return issues;
}

void TextureSetValidator::checkMatchTexture(const PBRTextureSet& ts, std::vector<ValidationIssue>& issues)
{
    if (ts.matchTexture.empty())
    {
        issues.push_back({ValidationSeverity::Error, "Vanilla match texture path is empty."});
    }
}

void TextureSetValidator::checkRequiredSlots(const PBRTextureSet& ts, std::vector<ValidationIssue>& issues)
{
    // Diffuse is always required
    if (ts.textures.find(PBRTextureSlot::Diffuse) == ts.textures.end())
    {
        issues.push_back({ValidationSeverity::Error, "Missing required texture: Diffuse (Base Color)."});
    }

    // Normal is always required
    if (ts.textures.find(PBRTextureSlot::Normal) == ts.textures.end())
    {
        issues.push_back({ValidationSeverity::Error, "Missing required texture: Normal Map."});
    }

    // RMAOS is required — either packed or split channels
    bool hasPackedRmaos = ts.textures.find(PBRTextureSlot::RMAOS) != ts.textures.end();
    bool hasSplitChannels = !ts.channelMaps.empty();
    if (ts.rmaosSourceMode == RMAOSSourceMode::PackedTexture && !hasPackedRmaos)
    {
        issues.push_back({ValidationSeverity::Error, "Missing required texture: RMAOS (packed mode selected but no RMAOS texture assigned)."});
    }
    if (ts.rmaosSourceMode == RMAOSSourceMode::SeparateChannels && !hasSplitChannels)
    {
        issues.push_back({ValidationSeverity::Error, "RMAOS split mode selected but no channel maps assigned."});
    }
}

void TextureSetValidator::checkResolutionConsistency(const PBRTextureSet& ts, std::vector<ValidationIssue>& issues)
{
    int refWidth = 0, refHeight = 0;
    std::string refName;

    for (const auto& [slot, entry] : ts.textures)
    {
        if (entry.width == 0 || entry.height == 0)
            continue;

        if (refWidth == 0)
        {
            refWidth = entry.width;
            refHeight = entry.height;
            refName = slotDisplayName(slot);
            continue;
        }

        if (entry.width != refWidth || entry.height != refHeight)
        {
            issues.push_back({ValidationSeverity::Warning,
                               std::format("{} resolution ({}x{}) differs from {} ({}x{}).",
                                           slotDisplayName(slot), entry.width, entry.height,
                                           refName, refWidth, refHeight)});
        }
    }
}

void TextureSetValidator::checkPowerOfTwo(const PBRTextureSet& ts, std::vector<ValidationIssue>& issues)
{
    for (const auto& [slot, entry] : ts.textures)
    {
        if (entry.width == 0 || entry.height == 0)
            continue;

        if (!isPowerOfTwo(entry.width) || !isPowerOfTwo(entry.height))
        {
            issues.push_back({ValidationSeverity::Warning,
                               std::format("{} has non-power-of-two resolution ({}x{}). "
                                           "This may cause issues with DDS compression and GPU performance.",
                                           slotDisplayName(slot), entry.width, entry.height)});
        }
    }
}

void TextureSetValidator::checkFeatureTextures(const PBRTextureSet& ts, std::vector<ValidationIssue>& issues)
{
    auto warnIfMissing = [&](bool feature, PBRTextureSlot slot, const char* featureName)
    {
        if (feature && ts.textures.find(slot) == ts.textures.end())
        {
            issues.push_back({ValidationSeverity::Warning,
                               std::format("{} is enabled but {} texture is not assigned.",
                                           featureName, slotDisplayName(slot))});
        }
    };

    warnIfMissing(ts.features.emissive, PBRTextureSlot::Emissive, "Emissive");
    warnIfMissing(ts.features.parallax || ts.features.coatParallax, PBRTextureSlot::Displacement, "Parallax");
    warnIfMissing(ts.features.subsurface, PBRTextureSlot::Subsurface, "Subsurface");
    warnIfMissing(ts.features.coatNormal, PBRTextureSlot::CoatNormalRoughness, "Coat Normal");
    warnIfMissing(ts.features.coatDiffuse, PBRTextureSlot::CoatColor, "Coat Diffuse");
}

void TextureSetValidator::checkSlotConflicts(const PBRTextureSet& ts, std::vector<ValidationIssue>& issues)
{
    // TX06 is shared by CoatNormalRoughness and Fuzz — can't have both
    bool hasCNR = ts.textures.find(PBRTextureSlot::CoatNormalRoughness) != ts.textures.end();
    bool hasFuzz = ts.textures.find(PBRTextureSlot::Fuzz) != ts.textures.end();
    if (hasCNR && hasFuzz)
    {
        issues.push_back({ValidationSeverity::Error,
                           "Coat Normal Roughness and Fuzz both assigned — they share NIF slot TX06 and cannot coexist."});
    }

    // TX07 is shared by Subsurface and CoatColor
    bool hasSS = ts.textures.find(PBRTextureSlot::Subsurface) != ts.textures.end();
    bool hasCoat = ts.textures.find(PBRTextureSlot::CoatColor) != ts.textures.end();
    if (hasSS && hasCoat)
    {
        issues.push_back({ValidationSeverity::Error,
                           "Subsurface and Coat Color both assigned — they share NIF slot TX07 and cannot coexist."});
    }

    // Feature conflict: fuzz and coat_normal are mutually exclusive
    if (ts.features.fuzz && ts.features.coatNormal)
    {
        issues.push_back({ValidationSeverity::Warning,
                           "Fuzz and Coat Normal are both enabled — they use the same NIF slot (TX06). Only one will be exported."});
    }
}

} // namespace tpbr
