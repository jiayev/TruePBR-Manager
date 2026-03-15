// Adapted from skyrim-community-shaders Common/BRDF.hlsli
//
// Academic references retained from original:
// - [Burley 2012, "Physically-Based Shading at Disney"]
// - [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
// - [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
// - [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
// - [Estevez et al. 2017, "Production Friendly Microfacet Sheen BRDF"]
// - [Neubelt et al. 2013, "Crafting a Next-gen Material Pipeline for The Order: 1886"]
// - [Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II"]
// - [Gotanda 2012/2014]
// - [Chan 2018, "Material Advances in Call of Duty: WWII"]
// - [Beckmann 1963, "The scattering of electromagnetic waves from rough surfaces"]
// - [Kutz et al. 2021, "Novel aspects of the Adobe Standard Material"]

#ifndef __BRDF_DEPENDENCY_HLSL__
#define __BRDF_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"

namespace BRDF
{
	// ── Diffuse BRDFs ──────────────────────────────────────

	float Diffuse_Lambert()
	{
		return 1.0 / Math::PI;
	}

	float3 Diffuse_Burley(float roughness, float NdotV, float NdotL, float VdotH)
	{
		float FD90 = 0.5 + 2.0 * VdotH * VdotH * roughness;
		float FdV = 1.0 + (FD90 - 1.0) * pow(1.0 - NdotV, 5.0);
		float FdL = 1.0 + (FD90 - 1.0) * pow(1.0 - NdotL, 5.0);
		return (1.0 / Math::PI) * (FdV * FdL);
	}

	// ── Fresnel ────────────────────────────────────────────

	float3 F_Schlick(float3 specularColor, float VdotH)
	{
		float Fc = pow(1 - VdotH, 5);
		return Fc + (1 - Fc) * specularColor;
	}

	float3 F_Schlick(float3 F0, float3 F90, float VdotH)
	{
		float Fc = pow(1 - VdotH, 5);
		return F0 + (F90 - F0) * Fc;
	}

	// ── Normal Distribution Functions ──────────────────────

	float D_GGX(float roughness, float NdotH)
	{
		float NdotH2 = NdotH * NdotH;
		float a = roughness * roughness;
		float a2 = a * a;
		float d = NdotH2 * (a2 - 1.0) + 1.0;
		return (a2 / (Math::PI * d * d));
	}

	float D_Charlie(float roughness, float NdotH)
	{
		float invAlpha = pow(abs(roughness), -4);
		float cos2h = NdotH * NdotH;
		float sin2h = 1.0 - cos2h;
		return (2.0 + invAlpha) * pow(abs(sin2h), invAlpha * 0.5) / Math::TAU;
	}

	// ── Visibility / Geometry ──────────────────────────────

	float Vis_SmithJointApprox(float roughness, float NdotV, float NdotL)
	{
		float a = roughness * roughness;
		float Vis_SmithV = NdotL * (NdotV * (1.0 + a) + a);
		float Vis_SmithL = NdotV * (NdotL * (1.0 + a) + a);
		return rcp(max(Vis_SmithV + Vis_SmithL, EPSILON_DIVISION)) * 0.5;
	}

	float Vis_Neubelt(float NdotV, float NdotL)
	{
		return rcp(4.0 * max(NdotL + NdotV - NdotL * NdotV, EPSILON_DIVISION));
	}

	// ── Environment BRDF ───────────────────────────────────

	float2 EnvBRDFApproxLazarov(float roughness, float NdotV)
	{
		const float4 c0 = { -1, -0.0275, -0.572, 0.022 };
		const float4 c1 = { 1, 0.0425, 1.04, -0.04 };
		float4 r = roughness * c0 + c1;
		float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
		float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
		return AB;
	}

	/// LUT-based EnvBRDF: apply pre-integrated BRDF factors from LUT to specular color.
	/// AB = float2 sampled from BRDF LUT at (NdotV, roughness).
	/// F90 handling: saturate(50.0 * specularColor.g) for dielectrics.
	float3 EnvBRDF(float3 specularColor, float2 AB)
	{
		float3 GF = specularColor * AB.x + saturate(50.0 * specularColor.g) * AB.y;
		return GF;
	}

	/// Analytical EnvBRDF fallback (Lazarov approximation).
	float2 EnvBRDF(float roughness, float NdotV)
	{
		return EnvBRDFApproxLazarov(roughness, NdotV);
	}
}

#endif // __BRDF_DEPENDENCY_HLSL__
