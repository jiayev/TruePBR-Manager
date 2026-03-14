// GT7 Tone Mapping — HLSL port of the reference C++ implementation
//
// Based on: https://blog.selfshadow.com/publications/s2025-shading-course/pdi/supplemental/gt7_tone_mapping.cpp
// UCS mode: ICtCp
//
// MIT License
//
// Copyright (c) 2025 Polyphony Digital Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef GT7_TONEMAP_HLSLI
#define GT7_TONEMAP_HLSLI

// Reference luminance: 1.0 in framebuffer = 100 cd/m^2
#define GT7_REFERENCE_LUMINANCE 100.0
// SDR reference white level (250 nits, as in Gran Turismo)
#define GT7_SDR_PAPER_WHITE 250.0

// ── Luminance scale conversion ─────────────────────────────

float gt7FbToPhysical(float v) { return v * GT7_REFERENCE_LUMINANCE; }
float gt7PhysicalToFb(float v) { return v / GT7_REFERENCE_LUMINANCE; }

// ── PQ (ST-2084) EOTF / inverse EOTF ──────────────────────

float gt7EotfPQ(float n)
{
    n = saturate(n);
    const float m1  = 0.1593017578125;
    const float m2  = 78.84375;
    const float c1  = 0.8359375;
    const float c2  = 18.8515625;
    const float c3  = 18.6875;
    const float pqC = 10000.0;

    float np = pow(n, 1.0 / m2);
    float l  = max(np - c1, 0.0) / (c2 - c3 * np);
    l = pow(max(l, 0.0), 1.0 / m1);
    return gt7PhysicalToFb(l * pqC);
}

float gt7InvEotfPQ(float v)
{
    const float m1  = 0.1593017578125;
    const float m2  = 78.84375;
    const float c1  = 0.8359375;
    const float c2  = 18.8515625;
    const float c3  = 18.6875;
    const float pqC = 10000.0;

    float y  = max(gt7FbToPhysical(v), 0.0) / pqC;
    float ym = pow(max(y, 0.0), m1);
    return pow((c1 + c2 * ym) / (1.0 + c3 * ym), m2);
}

// ── Rec.709 <-> Rec.2020 gamut conversion ──────────────────

float3 gt7Rec709ToRec2020(float3 c)
{
    return float3(
        dot(c, float3(0.6274040, 0.3292820, 0.0433136)),
        dot(c, float3(0.0690970, 0.9195400, 0.0113612)),
        dot(c, float3(0.0163916, 0.0880132, 0.8955950))
    );
}

float3 gt7Rec2020ToRec709(float3 c)
{
    return float3(
        dot(c, float3( 1.6604910, -0.5876411, -0.0728499)),
        dot(c, float3(-0.1245505,  1.1328999, -0.0083494)),
        dot(c, float3(-0.0181508, -0.1005789,  1.1187297))
    );
}

// ── ICtCp color space (Rec.2020 linear framebuffer input) ──

float3 gt7RgbToICtCp(float3 rgb)
{
    float l = (rgb.r * 1688.0 + rgb.g * 2146.0 + rgb.b * 262.0) / 4096.0;
    float m = (rgb.r *  683.0 + rgb.g * 2951.0 + rgb.b * 462.0) / 4096.0;
    float s = (rgb.r *   99.0 + rgb.g *  309.0 + rgb.b * 3688.0) / 4096.0;

    float lPQ = gt7InvEotfPQ(l);
    float mPQ = gt7InvEotfPQ(m);
    float sPQ = gt7InvEotfPQ(s);

    return float3(
        ( 2048.0 * lPQ +  2048.0 * mPQ                ) / 4096.0,
        ( 6610.0 * lPQ - 13613.0 * mPQ + 7003.0 * sPQ) / 4096.0,
        (17933.0 * lPQ - 17390.0 * mPQ -  543.0 * sPQ) / 4096.0
    );
}

float3 gt7ICtCpToRgb(float3 ictcp)
{
    float l = ictcp.x + 0.00860904 * ictcp.y + 0.11103  * ictcp.z;
    float m = ictcp.x - 0.00860904 * ictcp.y - 0.11103  * ictcp.z;
    float s = ictcp.x + 0.560031   * ictcp.y - 0.320627 * ictcp.z;

    float lLin = gt7EotfPQ(l);
    float mLin = gt7EotfPQ(m);
    float sLin = gt7EotfPQ(s);

    return float3(
        max( 3.43661   * lLin - 2.50645   * mLin + 0.0698454 * sLin, 0.0),
        max(-0.79133   * lLin + 1.9836    * mLin - 0.192271  * sLin, 0.0),
        max(-0.0259499 * lLin - 0.0989137 * mLin + 1.12486   * sLin, 0.0)
    );
}

// ── Tone curve helpers ─────────────────────────────────────

float gt7ChromaCurve(float x, float a, float b)
{
    return 1.0 - smoothstep(a, b, x);
}

// ── GT Tone Mapping curve evaluation ───────────────────────

float gt7EvalCurve(float x, float midPoint, float toeStrength,
                   float kA, float kB, float kC, float linThreshold)
{
    if (x <= 0.0) return 0.0;

    float wLin = smoothstep(0.0, midPoint, x);
    float wToe = 1.0 - wLin;

    float shoulder = kA + kB * exp(x * kC);

    if (x < linThreshold)
    {
        float toeMapped = midPoint * pow(x / midPoint, toeStrength);
        return wToe * toeMapped + wLin * x;
    }
    return shoulder;
}

// ── Main GT7 tone mapping ──────────────────────────────────
//
// Input:  linear Rec.709 RGB (scene-referred, 1.0 ≈ diffuse white)
// Output:
//   SDR: linear [0,1] Rec.709 — apply pow(1/2.2) for sRGB afterward
//   HDR: linear scRGB (1.0 = 80 nits)
//
// paperWhiteNits:       SDR white-point for HDR output (80–400, controls mid-tone brightness)
// peakBrightnessNits:   display peak luminance for HDR curve

float3 GT7ToneMap(float3 colorRec709, bool isHDR, float paperWhiteNits, float peakBrightnessNits)
{
    float3 inputColor = max(colorRec709, 0.0);

    // HDR: scale input so scene 1.0 → paperWhiteNits physical luminance
    float inputScale = isHDR ? (paperWhiteNits / GT7_REFERENCE_LUMINANCE) : 1.0;
    inputColor *= inputScale;

    // Convert Rec.709 → Rec.2020 (required for ICtCp)
    float3 rgb2020 = max(gt7Rec709ToRec2020(inputColor), 0.0);

    // Determine curve parameters
    float physicalTarget = isHDR ? peakBrightnessNits : GT7_SDR_PAPER_WHITE;
    float fbTarget       = gt7PhysicalToFb(physicalTarget);
    float sdrCorrection  = isHDR ? 1.0 : (1.0 / gt7PhysicalToFb(GT7_SDR_PAPER_WHITE));

    // Curve constants (matches initializeCurve)
    const float alpha      = 0.25;
    const float midPoint   = 0.538;
    const float linSection = 0.444;
    const float toeStr     = 1.280;

    float k  = (linSection - 1.0) / (alpha - 1.0);
    float kA = fbTarget * linSection + fbTarget * k;
    float kB = -fbTarget * k * exp(linSection / k);
    float kC = -1.0 / (k * fbTarget);
    float linThreshold = linSection * fbTarget;

    // Blend & chroma parameters
    const float blendRatio = 0.6;
    const float fadeStart  = 0.98;
    const float fadeEnd    = 1.16;

    // Target luminance in ICtCp
    float targetUcsLum = gt7RgbToICtCp(float3(fbTarget, fbTarget, fbTarget)).x;

    // Input → ICtCp for chroma
    float3 ucs = gt7RgbToICtCp(rgb2020);

    // Per-channel tone mapping ("skewed")
    float3 skewedRgb = float3(
        gt7EvalCurve(rgb2020.r, midPoint, toeStr, kA, kB, kC, linThreshold),
        gt7EvalCurve(rgb2020.g, midPoint, toeStr, kA, kB, kC, linThreshold),
        gt7EvalCurve(rgb2020.b, midPoint, toeStr, kA, kB, kC, linThreshold)
    );

    float3 skewedUcs = gt7RgbToICtCp(skewedRgb);

    // Chroma scaling
    float chromaScale = gt7ChromaCurve(ucs.x / targetUcsLum, fadeStart, fadeEnd);

    float3 scaledUcs = float3(
        skewedUcs.x,            // luminance from skewed
        ucs.y * chromaScale,    // chroma from original, scaled
        ucs.z * chromaScale
    );
    float3 scaledRgb = gt7ICtCpToRgb(scaledUcs);

    // Blend per-channel and chroma-preserved, apply correction + clamp
    float3 result;
    result.r = sdrCorrection * min((1.0 - blendRatio) * skewedRgb.r + blendRatio * scaledRgb.r, fbTarget);
    result.g = sdrCorrection * min((1.0 - blendRatio) * skewedRgb.g + blendRatio * scaledRgb.g, fbTarget);
    result.b = sdrCorrection * min((1.0 - blendRatio) * skewedRgb.b + blendRatio * scaledRgb.b, fbTarget);

    // Convert Rec.2020 → Rec.709
    float3 outRec709 = max(gt7Rec2020ToRec709(result), 0.0);

    // HDR: convert GT7 FB space (1.0 = 100 nits) → scRGB (1.0 = 80 nits)
    if (isHDR)
        outRec709 *= GT7_REFERENCE_LUMINANCE / 80.0;

    return outRec709;
}

#endif // GT7_TONEMAP_HLSLI
