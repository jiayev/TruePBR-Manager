#pragma once

#include "ModImporter.h"
#include "PBRTextureSet.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tpbr
{

/// Vanilla Skyrim texture type enumeration.
/// Maps to vanilla TX slots (TX00-TX07 in NIF).
enum class VanillaTextureType
{
    Diffuse,   ///< TX00: Base color
    Normal,    ///< TX01: Normal map
    Glow,      ///< TX02: Emissive/glow
    Parallax,  ///< TX03: Displacement/height
    Specular,  ///< TX07 or packed in Normal alpha: Specular intensity map
    BackLight, ///< TX07: Subsurface/backlight
    EnvMask,   ///< TX02: Environment mask (metallic mask)
    Cubemap,   ///< TX05: Environment cubemap for metallic tint
};

/// Specular map conversion mode for Blinn-Phong → PBR conversion.
enum class SpecularMode
{
    Direct,   ///< Use specular intensity directly
    DividePI, ///< Divide specular intensity by π for energy conservation
};

/// Per-texture gamma and brightness adjustment.
struct GammaBrightnessParams
{
    float gamma = 1.0f;      ///< Gamma exponent (1.0 = identity, typical 0.1–5.0)
    float brightness = 0.0f; ///< Brightness offset in linear space (-1.0–1.0)
};

/// Parameters controlling vanilla texture conversion.
struct VanillaConversionParams
{
    /// Blinn-Phong shininess exponent (default 50, typical range 0.1–10000)
    float shininess = 50.0f;

    /// Per-color-texture gamma/brightness adjustments.
    /// Keys are color textures: Diffuse, Glow, BackLight.
    /// Missing entries use identity (gamma=1.0, brightness=0.0).
    std::map<VanillaTextureType, GammaBrightnessParams> colorAdjustments;

    /// Specular map conversion mode (default Direct)
    SpecularMode specularMode = SpecularMode::Direct;

    /// Extract specular from Normal's alpha channel when enabled (default false)
    bool normalAlphaIsSpecular = false;

    /// Optional roughness override for metallic regions when cubemap tint is applied
    std::optional<float> metallicRoughnessOverride;

    /// Helper: get gamma/brightness for a texture type (returns identity if not set).
    GammaBrightnessParams getColorAdjustment(VanillaTextureType type) const
    {
        auto it = colorAdjustments.find(type);
        return it != colorAdjustments.end() ? it->second : GammaBrightnessParams{};
    }
};

/// Input parameters for vanilla texture conversion.
struct VanillaConversionInput
{
    /// Map of vanilla texture types to input file paths
    std::map<VanillaTextureType, std::filesystem::path> inputFiles;

    /// Conversion parameters
    VanillaConversionParams params;

    /// Output directory for generated files
    std::filesystem::path outputDir;

    /// Name for the generated texture set
    std::string textureSetName;

    /// Vanilla match path (e.g., "architecture/whiterun/wrwoodplank01")
    std::string vanillaMatchPath;
};

/// Result of vanilla texture conversion.
struct VanillaConversionResult
{
    /// Success status of the conversion
    bool success = false;

    /// Generated PBR texture set (valid if success == true)
    PBRTextureSet generatedSet;

    /// Diagnostic messages (Info, Warning, Error)
    std::vector<ImportDiagnostic> diagnostics;

    /// Map of generated output files per slot
    std::map<PBRTextureSlot, std::filesystem::path> generatedFiles;
};

/// Vanilla Skyrim Blinn-Phong texture converter to True PBR.
///
/// Converts vanilla Skyrim textures (Diffuse, Normal, Specular, EnvMask, Cubemap,
/// Glow, Parallax, BackLight) into True PBR texture sets compatible with PGPatcher export.
///
/// Features:
/// - Blinn-Phong → PBR roughness conversion using shininess exponent
/// - Specular map → RMAOS Specular channel
/// - EnvMask → RMAOS Metallic channel
/// - Cubemap average color → metallic tint overlay on Diffuse
/// - Normal alpha extraction → Specular channel (optional)
/// - Gamma/Brightness non-destructive adjustments
/// - Comprehensive diagnostics and validation
///
/// All methods are static (facade pattern), suitable for testing in isolation.
class VanillaConverter
{
  public:
    /// Progress callback: (current, total, description) → return false to cancel.
    using ProgressCallback = std::function<bool(int current, int total, const std::string& desc)>;

    /// Convert vanilla textures to a True PBR texture set.
    ///
    /// \param input Conversion input with vanilla texture files and parameters
    /// \param progress Optional progress callback for UI feedback
    /// \return Conversion result containing the generated PBRTextureSet and diagnostics
    static VanillaConversionResult convert(const VanillaConversionInput& input, ProgressCallback progress = nullptr);

    /// Convert Blinn-Phong shininess exponent to PBR roughness.
    ///
    /// Formula: `roughness = pow(2 / (2 + shininess), 0.25) * 255`
    ///
    /// \param shininess Blinn-Phong exponent (0.1–10000, typical 50)
    /// \return Roughness value in [0, 255] (uint8_t)
    static uint8_t shininessToRoughness(float shininess);

    /// Convert sRGB color channel to linear space.
    ///
    /// Uses the official sRGB transfer function (piecewise):
    /// - if x <= 0.04045: linear = x / 12.92
    /// - else: linear = pow((x + 0.055) / 1.055, 2.4)
    ///
    /// \param srgb sRGB color value in [0, 255]
    /// \return Linear space value in [0.0, 1.0]
    static float srgbToLinear(uint8_t srgb);

    /// Convert linear color channel to sRGB space.
    ///
    /// Uses the official sRGB transfer function (piecewise):
    /// - if x <= 0.0031308: srgb = x * 12.92
    /// - else: srgb = 1.055 * pow(x, 1/2.4) - 0.055
    ///
    /// \param linear Linear space value in [0.0, 1.0]
    /// \return sRGB color value in [0, 255] (uint8_t)
    static uint8_t linearToSrgb(float linear);

    /// Apply gamma and brightness adjustments to RGBA pixel data.
    ///
    /// Processes each pixel: convert RGB to linear → apply gamma curve → add brightness → convert back to sRGB.
    /// Alpha channel is unchanged. All operations in linear space.
    ///
    /// \param rgba Pointer to RGBA8 pixel data (width * height * 4 bytes)
    /// \param width Image width in pixels
    /// \param height Image height in pixels
    /// \param gamma Gamma exponent (1.0 = identity)
    /// \param brightness Brightness adjustment in linear space (-1.0–1.0)
    static void applyGammaBrightness(uint8_t* rgba, int width, int height, float gamma, float brightness);

    /// Extract alpha channel from RGBA pixel data.
    ///
    /// \param rgba Pointer to RGBA8 pixel data
    /// \param width Image width in pixels
    /// \param height Image height in pixels
    /// \return Vector of alpha values (one per pixel, width * height elements)
    static std::vector<uint8_t> extractAlphaChannel(const uint8_t* rgba, int width, int height);

    /// Validate conversion input for required and optional textures.
    ///
    /// Checks that Diffuse and Normal are present (required).
    /// Reports diagnostics for missing optional textures and unusual combinations.
    ///
    /// \param input Conversion input
    /// \param diags Output vector for diagnostic messages
    /// \return True if input is valid (Diffuse + Normal present), false otherwise
    static bool validateInput(const VanillaConversionInput& input, std::vector<ImportDiagnostic>& diags);

  private:
    /// Private constructor (static facade — no instances)
    VanillaConverter() = delete;
};

} // namespace tpbr
