// Adapted from skyrim-community-shaders Common/PBR.hlsli
//
// Feature-aware PBR lighting for TruePBR Manager preview.
// Supports: Subsurface, TwoLayer (Coat), Fuzz, HairMarschner.

#ifndef __PBR_DEPENDENCY_HLSL__
#define __PBR_DEPENDENCY_HLSL__

#include "Common/BRDF.hlsli"
#include "Common/Math.hlsli"
#include "Common/PBRMath.hlsli"
#include "Common/Shading.hlsli"
#include "Common/Glints/Glints2023.hlsli"

// ── Material Properties ────────────────────────────────────
// Simplified version of CS MaterialProperties (no Skyrim-specific fields).

struct PBRMaterial
{
	float3 BaseColor;
	float Roughness;
	float3 F0;
	float AO;
	float Metallic;

	// Subsurface
	float3 SubsurfaceColor;
	float Thickness;

	// Coat / Two-Layer
	float3 CoatColor;
	float CoatStrength;
	float CoatRoughness;
	float3 CoatF0;

	// Fuzz
	float3 FuzzColor;
	float FuzzWeight;

	// Glint
	float GlintLogMicrofacetDensity;
	float GlintMicrofacetRoughness;
	float GlintDensityRandomization;
	float GlintNoise;
	float3 Tangent;
	Glints::GlintCachedVars GlintCache;
};

namespace PBR
{
	// ── Hair Lighting (Marschner) ──────────────────────────

	float3 GetHairDiffuseColorMarschner(float3 N, float3 V, float3 L,
		float NdotL, float NdotV, float VdotL, float backlit, float area,
		PBRMaterial material)
	{
		float3 S = 0;

		float cosThetaL = sqrt(max(0, 1 - NdotL * NdotL));
		float cosThetaV = sqrt(max(0, 1 - NdotV * NdotV));
		float cosThetaD = sqrt((1 + cosThetaL * cosThetaV + NdotV * NdotL) / 2.0);

		const float3 Lp = L - NdotL * N;
		const float3 Vp = V - NdotV * N;
		const float cosPhi = dot(Lp, Vp) * rsqrt(dot(Lp, Lp) * dot(Vp, Vp) + EPSILON_DIVISION);
		const float cosHalfPhi = sqrt(saturate(0.5 + 0.5 * cosPhi));

		float n_prime = 1.19 / cosThetaD + 0.36 * cosThetaD;

		const float Shift = 0.0499f;
		const float Alpha[] = { -Shift * 2, Shift, Shift * 4 };
		float B[] = {
			area + material.Roughness,
			area + material.Roughness / 2,
			area + material.Roughness * 2
		};

		float hairIOR = HairIOR();
		float specularColor = IORToF0(hairIOR);

		float3 Tp;
		float Mp, Np, Fp, a, h, f;
		float ThetaH = NdotL + NdotV;

		// R
		Mp = HairGaussian(B[0], ThetaH - Alpha[0]);
		Np = 0.25 * cosHalfPhi;
		Fp = BRDF::F_Schlick(specularColor, sqrt(saturate(0.5 + 0.5 * VdotL))).x;
		S += (Mp * Np) * (Fp * lerp(1, backlit, saturate(-VdotL)));

		// TT
		Mp = HairGaussian(B[1], ThetaH - Alpha[1]);
		a = (1.55f / hairIOR) * rcp(n_prime);
		h = cosHalfPhi * (1 + a * (0.6 - 0.8 * cosPhi));
		f = BRDF::F_Schlick(specularColor, cosThetaD * sqrt(saturate(1 - h * h))).x;
		Fp = (1 - f) * (1 - f);
		Tp = pow(abs(material.BaseColor), 0.5 * sqrt(1 - (h * a) * (h * a)) / cosThetaD);
		Np = exp(-3.65 * cosPhi - 3.98);
		S += (Mp * Np) * (Fp * Tp) * backlit;

		// TRT
		Mp = HairGaussian(B[2], ThetaH - Alpha[2]);
		f = BRDF::F_Schlick(specularColor, cosThetaD * 0.5f).x;
		Fp = (1 - f) * (1 - f) * f;
		Tp = pow(abs(material.BaseColor), 0.8 / cosThetaD);
		Np = exp(17 * cosPhi - 16.78);
		S += (Mp * Np) * (Fp * Tp);

		return S;
	}

	float3 GetHairDiffuseAttenuationKajiyaKay(float3 N, float3 V, float3 L,
		float NdotL, float NdotV, float shadow, PBRMaterial material)
	{
		float3 S = 0;

		float diffuseKajiya = 1 - abs(NdotL);

		float3 fakeN = normalize(V - N * NdotV);
		const float wrap = 1;
		float wrappedNdotL = saturate((dot(fakeN, L) + wrap) / ((1 + wrap) * (1 + wrap)));
		float diffuseScatter = (1 / Math::PI) * lerp(wrappedNdotL, diffuseKajiya, 0.33);
		float luma = dot(material.BaseColor, float3(0.2125, 0.7154, 0.0722));
		float3 scatterTint = pow(material.BaseColor / max(luma, 1e-5), 1 - shadow);
		S += sqrt(material.BaseColor) * diffuseScatter * scatterTint;

		return S;
	}

	float3 GetHairColorMarschner(float3 N, float3 V, float3 L,
		float NdotL, float NdotV, float VdotL, float shadow, float backlit, float area,
		PBRMaterial material)
	{
		float3 color = 0;
		color += GetHairDiffuseColorMarschner(N, V, L, NdotL, NdotV, VdotL, backlit, area, material);
		color += GetHairDiffuseAttenuationKajiyaKay(N, V, L, NdotL, NdotV, shadow, material);
		return color;
	}

	// ── Direct Lighting ────────────────────────────────────

	struct DirectLightResult
	{
		float3 diffuse;
		float3 specular;
		float3 transmission;
		float3 coatDiffuse;
	};

	DirectLightResult GetDirectLight(float3 N, float3 V, float3 L, float3 lightColor,
		PBRMaterial material, uint featureFlags)
	{
		DirectLightResult result = (DirectLightResult)0;

		float3 H = normalize(V + L);

		float NdotL = dot(N, L);
		float NdotV = dot(N, V);
		float VdotL = dot(V, L);
		float NdotH = dot(N, H);
		float VdotH = dot(V, H);

		float satNdotL = clamp(NdotL, EPSILON_DOT_CLAMP, 1);
		float satNdotV = saturate(abs(NdotV) + EPSILON_DOT_CLAMP);
		float satNdotH = saturate(NdotH);
		float satVdotH = saturate(VdotH);

		[branch] if ((featureFlags & Flags::HairMarschner) != 0)
		{
			result.transmission += lightColor * GetHairColorMarschner(
				N, V, L, NdotL, NdotV, VdotL, 0, 1, 0, material);
		}
		else
		{
			// ── Specular lobe: Fr = D·G·F ──
			float3 F;
			[branch] if ((featureFlags & Flags::Glint) != 0 && material.GlintLogMicrofacetDensity > 1.1)
			{
				float D = BRDF::D_GGX(material.Roughness, satNdotH);
				float D_max = BRDF::D_GGX(material.Roughness, 1);
				float glintH = dot(material.Tangent, H);
				D = Glints::SampleGlints2023NDF(material.GlintNoise,
					material.GlintLogMicrofacetDensity, material.GlintMicrofacetRoughness,
					material.GlintDensityRandomization, material.GlintCache, glintH, D, D_max).x;
				float G = BRDF::Vis_SmithJointApprox(material.Roughness, satNdotV, satNdotL);
				F = BRDF::F_Schlick(material.F0, satVdotH);
				result.specular = (D * G * F) * lightColor * satNdotL;
			}
			else
			{
				result.specular = GetSpecularDirectLightMultiplierMicrofacet(
					material.Roughness, material.F0,
					satNdotL, satNdotV, satNdotH, satVdotH, F) * lightColor * satNdotL;
			}

			// ── Diffuse lobe: Fd = (1-F)·(1-metallic)·baseColor·Burley ──
			// Energy conservation:
			//   (1-F): specularly reflected light cannot also diffuse
			//   (1-metallic): metals have no subsurface diffuse response
			float3 kD = (1.0 - F) * (1.0 - material.Metallic);
			float3 Fd = BRDF::Diffuse_Burley(material.Roughness, satNdotV, satNdotL, satVdotH);
			result.diffuse = kD * material.BaseColor * Fd * lightColor * satNdotL;

			// ── Fuzz / sheen lobe (replaces base specular partially) ──
			[branch] if ((featureFlags & Flags::Fuzz) != 0)
			{
				float3 fuzzSpecular = GetSpecularDirectLightMultiplierMicroflakes(
					material.Roughness, material.FuzzColor,
					satNdotL, satNdotV, satNdotH, satVdotH) * lightColor * satNdotL;
				result.specular = lerp(result.specular, fuzzSpecular, material.FuzzWeight);
			}

			// ── Subsurface transmission ──
			[branch] if ((featureFlags & Flags::Subsurface) != 0)
			{
				const float subsurfacePower = 12.234;
				float forwardScatter = exp2(saturate(-VdotL) * subsurfacePower - subsurfacePower);
				float backScatter = saturate(satNdotL * material.Thickness + (1.0 - material.Thickness)) * 0.5;
				float subsurface = lerp(backScatter, 1, forwardScatter) * (1.0 - material.Thickness);
				result.transmission = kD * material.SubsurfaceColor * subsurface * lightColor * BRDF::Diffuse_Lambert();
			}
			else if ((featureFlags & Flags::TwoLayer) != 0)
			{
				float coatNdotL = satNdotL;
				float coatNdotV = satNdotV;
				float coatNdotH = satNdotH;
				float coatVdotH = satVdotH;

				float3 coatF;
				float3 coatSpecular = GetSpecularDirectLightMultiplierMicrofacet(
					material.CoatRoughness, material.CoatF0,
					coatNdotL, coatNdotV, coatNdotH, coatVdotH, coatF) * lightColor * coatNdotL;

				// Coat Fresnel attenuation: light reflected by coat cannot reach base
				float3 layerAttenuation = 1 - coatF * material.CoatStrength;
				result.diffuse *= layerAttenuation;
				result.specular *= layerAttenuation;

				// Coat diffuse: non-reflected light through colored coat
				result.coatDiffuse = (1.0 - coatF) * lightColor * coatNdotL * BRDF::Diffuse_Lambert();
				result.specular += coatSpecular * material.CoatStrength;
			}
		}

		return result;
	}

	// ── Indirect Lighting Weights ──────────────────────────

	struct IndirectLobeWeights
	{
		float3 diffuse;
		float3 specular;
		float3 coatSpecular;
	};

	IndirectLobeWeights GetIndirectLobeWeights(float3 N, float3 V,
		PBRMaterial material, uint featureFlags, float2 envBRDF_AB)
	{
		IndirectLobeWeights weights = (IndirectLobeWeights)0;

		float NdotV = saturate(dot(N, V));

		[branch] if ((featureFlags & Flags::HairMarschner) != 0)
		{
			float3 L = normalize(V - N * dot(V, N));
			float nl = dot(N, L);
			float vl = dot(V, L);
			weights.diffuse = GetHairColorMarschner(N, V, L, nl, NdotV, vl, 1, 0, 0.2, material);
		}
		else
		{
			weights.diffuse = material.BaseColor;

			[branch] if ((featureFlags & Flags::Subsurface) != 0)
			{
				weights.diffuse += material.SubsurfaceColor * (1 - material.Thickness) / Math::PI;
			}
			[branch] if ((featureFlags & Flags::Fuzz) != 0)
			{
				weights.diffuse += material.FuzzColor * material.FuzzWeight;
			}

			// Use LUT-based EnvBRDF with UE F90 handling
			weights.specular = BRDF::EnvBRDF(material.F0, envBRDF_AB);

			// Energy conservation: use integrated specular reflectance from EnvBRDF
			// (1 - specularWeight) is the energy not reflected specularly, available for diffuse
			weights.diffuse *= (1 - weights.specular) * (1 - material.Metallic);

			[branch] if ((featureFlags & Flags::TwoLayer) != 0)
			{
				float2 coatAB = BRDF::EnvBRDF(material.CoatRoughness, NdotV);
				float3 coatSpecularLobeWeight = BRDF::EnvBRDF(material.CoatF0, coatAB);

				// Use integrated coat reflectance for layer attenuation
				float3 layerAttenuation = 1 - coatSpecularLobeWeight * material.CoatStrength;
				weights.diffuse *= layerAttenuation;
				weights.specular *= layerAttenuation;

				[branch] if ((featureFlags & Flags::ColoredCoat) != 0)
				{
					float3 coatDiffuseLobeWeight = material.CoatColor * (1 - coatSpecularLobeWeight);
					weights.diffuse += coatDiffuseLobeWeight * material.CoatStrength;
				}
				weights.coatSpecular = coatSpecularLobeWeight * material.CoatStrength;
			}
		}

		return weights;
	}
}

#endif // __PBR_DEPENDENCY_HLSL__
