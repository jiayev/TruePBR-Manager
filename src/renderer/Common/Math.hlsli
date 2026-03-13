// Adapted from skyrim-community-shaders Common/Math.hlsli
#ifndef __MATH_DEPENDENCY_HLSL__
#define __MATH_DEPENDENCY_HLSL__

#define EPSILON_SSS_ALBEDO 1e-3f
#define EPSILON_DOT_CLAMP 1e-5f
#define EPSILON_DIVISION 1e-6f

namespace Math
{
	static const float PI = 3.1415926535897932384626433832795f;
	static const float HALF_PI = PI * 0.5f;
	static const float TAU = PI * 2.0f;
}

#endif // __MATH_DEPENDENCY_HLSL__
