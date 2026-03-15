#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace tpbr
{

/// PBR texture slot — corresponds to NIF TX00..TX07
enum class PBRTextureSlot
{
    Diffuse,             // Slot 1 (TX00): Base Color RGB + Opacity A
    Normal,              // Slot 2 (TX01): Normal Map RGB
    Emissive,            // Slot 3 (TX02): Glow/Emissive RGB
    Displacement,        // Slot 4 (TX03): Height R
    RMAOS,               // Slot 6 (TX05): Roughness R, Metallic G, AO B, Specular A
    CoatNormalRoughness, // Slot 7 (TX06): Coat Normal RGB + Coat Roughness A
    Fuzz,                // Slot 7 (TX06): Fuzz Color RGB + Fuzz Mask A
    Subsurface,          // Slot 8 (TX07): Subsurface Color RGB + Opacity A
    CoatColor,           // Slot 8 (TX07): Coat Color RGB + Strength A
};

/// Individual channel maps for RMAOS packing
enum class ChannelMap
{
    Roughness,
    Metallic,
    AO,
    Specular,
};

/// Defines whether RMAOS comes from a packed texture or from split channels.
enum class RMAOSSourceMode
{
    PackedTexture,
    SeparateChannels,
};

enum class TextureMatchMode
{
    Auto,
    Diffuse,
    Normal,
};

/// Output DDS compression mode for exported textures.
enum class DDSCompressionMode
{
    BC7_sRGB,
    BC7_Linear,
    BC3_sRGB,
    BC6H_UF16,
    BC5_Linear,
    BC4_Linear,
    BC1_sRGB,
    BC1_Linear,
    RGBA8_sRGB,
    RGBA8_Linear,
};

enum class TextureAlphaMode
{
    Unknown,
    None,
    Opaque,
    Transparent,
};

/// Get the standard DDS suffix for a given slot
const char* slotSuffix(PBRTextureSlot slot);

/// Get display name for a slot
const char* slotDisplayName(PBRTextureSlot slot);

/// Get the default export compression mode for a slot.
DDSCompressionMode defaultCompressionForSlot(PBRTextureSlot slot);

/// Get UI display name for a compression mode.
const char* compressionModeDisplayName(DDSCompressionMode mode);

/// Get stable serialization key for a compression mode.
const char* compressionModeKey(DDSCompressionMode mode);

/// Parse a serialized compression mode key.
bool tryParseCompressionMode(const std::string& value, DDSCompressionMode& mode);

/// Get stable serialization key for the active RMAOS source mode.
const char* rmaosSourceModeKey(RMAOSSourceMode mode);

/// Parse a serialized RMAOS source mode key.
bool tryParseRmaosSourceMode(const std::string& value, RMAOSSourceMode& mode);

/// Get stable serialization key for the vanilla match mode.
const char* textureMatchModeKey(TextureMatchMode mode);

/// Get UI display name for the vanilla match mode.
const char* textureMatchModeDisplayName(TextureMatchMode mode);

/// Parse a serialized vanilla match mode key.
bool tryParseTextureMatchMode(const std::string& value, TextureMatchMode& mode);

/// Generate a list of export size options based on a texture's native size.
/// Returns pairs of (width, height) including the original and several
/// power-of-two scales above and below. The first entry is always the
/// original size.
std::vector<std::pair<int, int>> generateExportSizeOptions(int nativeWidth, int nativeHeight);

// ─── Texture Entry ──────────────────────────────────────────

struct TextureEntry
{
    std::filesystem::path sourcePath; // Original imported file
    PBRTextureSlot slot{};
    int width = 0;
    int height = 0;
    int channels = 0;
    TextureAlphaMode alphaMode = TextureAlphaMode::Unknown;
    std::string format; // "png", "dds", "tga", etc.
};

struct ChannelMapEntry
{
    std::filesystem::path sourcePath;
    int width = 0;
    int height = 0;
    int channels = 0;
    std::string format;
};

// ─── PBR Feature Flags ─────────────────────────────────────

struct PBRFeatureFlags
{
    bool emissive = false;
    bool parallax = false;
    bool subsurface = false;
    bool subsurfaceFoliage = false;
    bool multilayer = false;
    bool coatDiffuse = false;
    bool coatParallax = false;
    bool coatNormal = false;
    bool fuzz = false;
    bool glint = false;
    bool hair = false;
};

// ─── PBR Parameters ────────────────────────────────────────

struct PBRParameters
{
    float specularLevel = 0.04f;
    float roughnessScale = 1.0f;
    float displacementScale = 1.0f;
    float subsurfaceOpacity = 1.0f;
    std::array<float, 3> subsurfaceColor = {1.0f, 1.0f, 1.0f};

    // Emissive
    float emissiveScale = 0.0f;

    // Multilayer / Coat
    float coatStrength = 0.0f;
    float coatRoughness = 0.0f;
    float coatSpecularLevel = 0.04f;

    // Fuzz
    std::array<float, 3> fuzzColor = {1.0f, 1.0f, 1.0f};
    float fuzzWeight = 1.0f;

    // Glint
    float glintScreenSpaceScale = 0.0f;
    float glintLogMicrofacetDensity = 0.0f;
    float glintMicrofacetRoughness = 0.0f;
    float glintDensityRandomization = 0.0f;

    // Mesh tweaks
    bool vertexColors = true;
    float vertexColorLumMult = 1.0f;
    float vertexColorSatMult = 1.0f;
};

// ─── PBR Texture Set ───────────────────────────────────────

/// One PBR texture set maps to one vanilla texture path.
struct PBRTextureSet
{
    std::string name;         // Display name (e.g. "WhiterunWoodPlank01")
    std::string matchTexture; // Vanilla diffuse path to match (e.g. "architecture\\whiterun\\wrwoodplank01")
    TextureMatchMode matchMode = TextureMatchMode::Auto;

    /// Assigned textures per slot
    std::map<PBRTextureSlot, TextureEntry> textures;

    /// Export compression override per slot.
    std::map<PBRTextureSlot, DDSCompressionMode> exportCompression;

    /// Active RMAOS authoring mode.
    RMAOSSourceMode rmaosSourceMode = RMAOSSourceMode::PackedTexture;

    /// Individual channel maps before RMAOS packing
    std::map<ChannelMap, ChannelMapEntry> channelMaps;

    PBRFeatureFlags features;
    PBRParameters params;

    /// Optional: Landscape TXST EDIDs this texture set also applies to.
    /// If non-empty, exporter generates PBRTextureSets/<edid>.json for each.
    /// The textures are shared with NIF export — no separate landscape texture output.
    std::vector<std::string> landscapeEdids;

    /// Optional per-slot export size overrides. When set, the texture will be
    /// resized to this resolution during export. {0,0} means use original size.
    std::map<PBRTextureSlot, std::pair<int, int>> exportSize;

    /// Optional per-slot path overrides. When set, the exporter uses the given
    /// full relative path (e.g. "textures\\pbr\\custom\\tex.dds") as a PGPatcher
    /// slot command instead of deriving the path automatically.
    std::map<PBRTextureSlot, std::string> slotPathOverrides;

    std::string tags;
    std::string notes;
};

} // namespace tpbr
