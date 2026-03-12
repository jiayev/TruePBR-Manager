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
    case PBRTextureSlot::Diffuse:             return "Diffuse (Base Color)";
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

} // namespace tpbr
