// Adapted from skyrim-community-shaders Common/PBRMath.hlsli
//
// PBR math utilities, flag definitions, and specular BRDF functions.

#ifndef __PBR_MATH_HLSL__
#define __PBR_MATH_HLSL__

#include "Common/BRDF.hlsli"
#include "Common/Math.hlsli"

namespace PBR
{
	namespace Constants
	{
		static const float MinRoughness = 0.04f;
		static const float MaxRoughness = 1.0f;
		static const float MinGlintDensity = 1.0f;
		static const float MaxGlintDensity = 40.0f;
		static const float MinGlintRoughness = 0.005f;
		static const float MaxGlintRoughness = 0.3f;
		static const float MinGlintDensityRandomization = 0.0f;
		static const float MaxGlintDensityRandomization = 5.0f;
	}

	namespace Flags
	{
		static const uint HasEmissive = (1 << 0);
		static const uint HasDisplacement = (1 << 1);
		static const uint HasFeatureTexture0 = (1 << 2);
		static const uint HasFeatureTexture1 = (1 << 3);
		static const uint Subsurface = (1 << 4);
		static const uint TwoLayer = (1 << 5);
		static const uint ColoredCoat = (1 << 6);
		static const uint InterlayerParallax = (1 << 7);
		static const uint CoatNormal = (1 << 8);
		static const uint Fuzz = (1 << 9);
		static const uint HairMarschner = (1 << 10);
		static const uint Glint = (1 << 11);
	}

	/// Specular BRDF: GGX microfacet model (D * Vis * F)
	float3 GetSpecularDirectLightMultiplierMicrofacet(float roughness, float3 specularColor,
		float NdotL, float NdotV, float NdotH, float VdotH, out float3 F)
	{
		float D = BRDF::D_GGX(roughness, NdotH);
		float G = BRDF::Vis_SmithJointApprox(roughness, NdotV, NdotL);
		F = BRDF::F_Schlick(specularColor, VdotH);
		return D * G * F;
	}

	/// Specular BRDF: Charlie microflake model (for sheen/fuzz)
	float3 GetSpecularDirectLightMultiplierMicroflakes(float roughness, float3 specularColor,
		float NdotL, float NdotV, float NdotH, float VdotH)
	{
		float D = BRDF::D_Charlie(roughness, NdotH);
		float G = BRDF::Vis_Neubelt(NdotV, NdotL);
		float3 F = BRDF::F_Schlick(specularColor, VdotH);
		return D * G * F;
	}

	/// Hair IOR calculation (Marschner model)
	float HairIOR()
	{
		const float n = 1.55;
		const float a = 1;
		float ior1 = 2 * (n - 1) * (a * a) - n + 2;
		float ior2 = 2 * (n - 1) / (a * a) - n + 2;
		return 0.5f * ((ior1 + ior2) + 0.5f * (ior1 - ior2));
	}

	float IORToF0(float IOR)
	{
		return pow((1 - IOR) / (1 + IOR), 2);
	}

	float HairGaussian(float B, float Theta)
	{
		float B_safe = max(B, 1e-6);
		return exp(-0.5 * Theta * Theta / (B_safe * B_safe)) / (sqrt(Math::TAU) * B_safe);
	}
}

#endif // __PBR_MATH_HLSL__
