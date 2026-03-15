// Color Space Conversion Utilities for ACES Pipeline
//
// Working color space: ACEScg (AP1 primaries, ACES white point ~D60)
//
// Matrices include Bradford chromatic adaptation between D65 (sRGB/Rec.709)
// and the ACES white point (approximately D60, CIE 1931 xy = 0.32168, 0.33767).
//
// References:
// - Academy S-2014-004: ACEScg — A Working Space for CGI Render and Compositing
// - SMPTE ST 2065-1:2021 — Academy Color Encoding Specification (ACES)
// - IEC 61966-2-1:1999 — sRGB standard (EOTF/OETF specification)

#ifndef __COLOR_SPACES_HLSLI__
#define __COLOR_SPACES_HLSLI__

namespace ColorSpaces
{

// ── AP1 (ACEScg) luminance coefficients ────────────────────
// Derived from AP1 primaries and ACES white point.
static const float3 AP1_RGB2Y = float3(0.2722287168, 0.6740817658, 0.0536895174);

// ── sRGB / Rec.709 (D65) ↔ ACEScg (AP1, ACES white) ──────
// These matrices incorporate Bradford chromatic adaptation D65 → ACES white.

float3 sRGBToACEScg(float3 srgb)
{
    // Linear Rec.709 (D65) → ACEScg (AP1, ACES white)
    return float3(
        dot(srgb, float3( 0.61319, 0.33951, 0.04737)),
        dot(srgb, float3( 0.07021, 0.91634, 0.01345)),
        dot(srgb, float3( 0.02062, 0.10957, 0.86981))
    );
}

float3 ACEScgToSRGB(float3 acescg)
{
    // ACEScg (AP1, ACES white) → Linear Rec.709 (D65)
    return float3(
        dot(acescg, float3( 1.70505, -0.62179, -0.08326)),
        dot(acescg, float3(-0.13026,  1.14080, -0.01055)),
        dot(acescg, float3(-0.02400, -0.12897,  1.15297))
    );
}

// ── sRGB OETF (IEC 61966-2-1) ─────────────────────────────
// Encodes linear light to sRGB non-linear signal.
// This is the correct piecewise function, NOT the simplified pow(1/2.2).

float sRGBOETFChannel(float linearVal)
{
    return (linearVal <= 0.0031308)
        ? linearVal * 12.92
        : 1.055 * pow(linearVal, 1.0 / 2.4) - 0.055;
}

float3 sRGBOETF(float3 linearColor)
{
    return float3(
        sRGBOETFChannel(linearColor.x),
        sRGBOETFChannel(linearColor.y),
        sRGBOETFChannel(linearColor.z)
    );
}

// ── sRGB EOTF (inverse of OETF) ──────────────────────────
// Decodes sRGB non-linear signal to linear light.

float sRGBEOTFChannel(float srgb)
{
    return (srgb <= 0.04045)
        ? srgb / 12.92
        : pow((srgb + 0.055) / 1.055, 2.4);
}

float3 sRGBEOTF(float3 srgbColor)
{
    return float3(
        sRGBEOTFChannel(srgbColor.x),
        sRGBEOTFChannel(srgbColor.y),
        sRGBEOTFChannel(srgbColor.z)
    );
}

} // namespace ColorSpaces

#endif // __COLOR_SPACES_HLSLI__
