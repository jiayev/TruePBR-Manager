#include "PBRTextureSet.h"

namespace tpbr {

const char* slotSuffix(PBRTextureSlot slot)
{
    switch (slot) {
    case PBRTextureSlot::Diffuse:             return ".dds";
    case PBRTextureSlot::Normal:              return "_n.dds";
    case PBRTextureSlot::Emissive:            return "_g.dds";
    case PBRTextureSlot::Displacement:        return "_p.dds";
    case PBRTextureSlot::RMAOS:              return "_rmaos.dds";
    case PBRTextureSlot::CoatNormalRoughness: return "_cnr.dds";
    case PBRTextureSlot::Fuzz:               return "_f.dds";
    case PBRTextureSlot::Subsurface:         return "_s.dds";
    case PBRTextureSlot::CoatColor:          return "_s.dds";
    default:                                  return ".dds";
    }
}

const char* slotDisplayName(PBRTextureSlot slot)
{
    switch (slot) {
    case PBRTextureSlot::Diffuse:             return "Albedo (Base Color)";
    case PBRTextureSlot::Normal:              return "Normal Map";
    case PBRTextureSlot::Emissive:            return "Emissive / Glow";
    case PBRTextureSlot::Displacement:        return "Displacement / Height";
    case PBRTextureSlot::RMAOS:              return "RMAOS (Packed)";
    case PBRTextureSlot::CoatNormalRoughness: return "Coat Normal + Roughness";
    case PBRTextureSlot::Fuzz:               return "Fuzz";
    case PBRTextureSlot::Subsurface:         return "Subsurface";
    case PBRTextureSlot::CoatColor:          return "Coat Color + Strength";
    default:                                  return "Unknown";
    }
}

DDSCompressionMode defaultCompressionForSlot(PBRTextureSlot slot)
{
    switch (slot) {
    case PBRTextureSlot::Diffuse:
    case PBRTextureSlot::Subsurface:
    case PBRTextureSlot::Fuzz:
    case PBRTextureSlot::CoatColor:
        return DDSCompressionMode::BC7_sRGB;

    case PBRTextureSlot::Emissive:
        return DDSCompressionMode::BC6H_UF16;

    case PBRTextureSlot::Displacement:
        return DDSCompressionMode::BC4_Linear;

    case PBRTextureSlot::Normal:
    case PBRTextureSlot::RMAOS:
    case PBRTextureSlot::CoatNormalRoughness:
    default:
        return DDSCompressionMode::BC7_Linear;
    }
}

const char* compressionModeDisplayName(DDSCompressionMode mode)
{
    switch (mode) {
    case DDSCompressionMode::BC7_sRGB:    return "BC7 sRGB";
    case DDSCompressionMode::BC7_Linear:  return "BC7 Linear";
    case DDSCompressionMode::BC6H_UF16:   return "BC6H UF16";
    case DDSCompressionMode::BC5_Linear:  return "BC5 Linear";
    case DDSCompressionMode::BC4_Linear:  return "BC4 Linear";
    case DDSCompressionMode::BC1_sRGB:    return "BC1 sRGB";
    case DDSCompressionMode::BC1_Linear:  return "BC1 Linear";
    case DDSCompressionMode::RGBA8_sRGB:  return "RGBA8 sRGB";
    case DDSCompressionMode::RGBA8_Linear: return "RGBA8 Linear";
    default:                               return "BC7 Linear";
    }
}

const char* compressionModeKey(DDSCompressionMode mode)
{
    switch (mode) {
    case DDSCompressionMode::BC7_sRGB:    return "bc7_srgb";
    case DDSCompressionMode::BC7_Linear:  return "bc7_linear";
    case DDSCompressionMode::BC6H_UF16:   return "bc6h_uf16";
    case DDSCompressionMode::BC5_Linear:  return "bc5_linear";
    case DDSCompressionMode::BC4_Linear:  return "bc4_linear";
    case DDSCompressionMode::BC1_sRGB:    return "bc1_srgb";
    case DDSCompressionMode::BC1_Linear:  return "bc1_linear";
    case DDSCompressionMode::RGBA8_sRGB:  return "rgba8_srgb";
    case DDSCompressionMode::RGBA8_Linear: return "rgba8_linear";
    default:                               return "bc7_linear";
    }
}

bool tryParseCompressionMode(const std::string& value, DDSCompressionMode& mode)
{
    if (value == "bc7_srgb") {
        mode = DDSCompressionMode::BC7_sRGB;
        return true;
    }
    if (value == "bc7_linear") {
        mode = DDSCompressionMode::BC7_Linear;
        return true;
    }
    if (value == "bc6h_uf16") {
        mode = DDSCompressionMode::BC6H_UF16;
        return true;
    }
    if (value == "bc5_linear") {
        mode = DDSCompressionMode::BC5_Linear;
        return true;
    }
    if (value == "bc4_linear") {
        mode = DDSCompressionMode::BC4_Linear;
        return true;
    }
    if (value == "bc1_srgb") {
        mode = DDSCompressionMode::BC1_sRGB;
        return true;
    }
    if (value == "bc1_linear") {
        mode = DDSCompressionMode::BC1_Linear;
        return true;
    }
    if (value == "rgba8_srgb") {
        mode = DDSCompressionMode::RGBA8_sRGB;
        return true;
    }
    if (value == "rgba8_linear") {
        mode = DDSCompressionMode::RGBA8_Linear;
        return true;
    }

    return false;
}

} // namespace tpbr
