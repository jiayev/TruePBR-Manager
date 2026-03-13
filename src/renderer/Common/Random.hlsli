// Adapted from skyrim-community-shaders Common/Random.hlsli
//
// Random number generation utilities for GPU shaders.

#ifndef __RANDOM_HLSLI__
#define __RANDOM_HLSLI__

namespace Random
{
	///////////////////////////////////////////////////////////
	// WHITE-LIKE HASHES
	///////////////////////////////////////////////////////////

	// https://www.shadertoy.com/view/XlGcRh

	uint2 pcg2d(uint2 v)
	{
		v = v * 1664525u + 1013904223u;

		v.x += v.y * 1664525u;
		v.y += v.x * 1664525u;

		v = v ^ (v >> 16u);

		v.x += v.y * 1664525u;
		v.y += v.x * 1664525u;

		v = v ^ (v >> 16u);

		return v;
	}

	uint3 pcg3d(uint3 v)
	{
		v = v * 1664525u + 1013904223u;

		v.x += v.y * v.z;
		v.y += v.z * v.x;
		v.z += v.x * v.y;

		v ^= v >> 16u;

		v.x += v.y * v.z;
		v.y += v.z * v.x;
		v.z += v.x * v.y;

		return v;
	}

	///////////////////////////////////////////////////////////
	// LOW DISCREPANCY SEQUENCES
	///////////////////////////////////////////////////////////

	// https://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
	float R1Modified(float idx, float seed = 0)
	{
		return frac(seed + idx * 0.38196601125);
	}
}

#endif // __RANDOM_HLSLI__
