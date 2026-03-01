#define SHADOW_MAP_CASCADE_COUNT 4

// Poisson disk samples for PCSS
const vec2 poissonDisk[16] = vec2[](
	vec2(-0.94201624, -0.39906216),
	vec2( 0.94558609, -0.76890725),
	vec2(-0.09418410, -0.92938870),
	vec2( 0.34495938,  0.29387760),
	vec2(-0.91588581,  0.45771432),
	vec2(-0.81544232, -0.87912464),
	vec2(-0.38277543,  0.27676845),
	vec2( 0.97484398,  0.75648379),
	vec2( 0.44323325, -0.97511554),
	vec2( 0.53742981, -0.47373420),
	vec2(-0.26496911, -0.41893023),
	vec2( 0.79197514,  0.19090188),
	vec2(-0.24188840,  0.99706507),
	vec2(-0.81409955,  0.91437590),
	vec2( 0.19984126,  0.78641367),
	vec2( 0.14383161, -0.14100790)
);

// PCSS constants
const int BLOCKER_SEARCH_SAMPLES = 16;
const int PCF_SAMPLES = 16;

// Per-fragment random rotation angle using interleaved gradient noise
float InterleavedGradientNoise(vec2 screenPos)
{
	vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
	return fract(magic.z * fract(dot(screenPos, magic.xy)));
}

// Rotate a 2D offset by angle (in radians)
vec2 RotateOffset(vec2 offset, float cosAngle, float sinAngle)
{
	return vec2(
		offset.x * cosAngle - offset.y * sinAngle,
		offset.x * sinAngle + offset.y * cosAngle
	);
}

// Step 1: Find average blocker depth in the search region
float FindBlockerDepth(sampler2DArray shadowMap, vec3 shadowCoord, uint cascadeIndex,
	float searchRadius, vec2 texelSize, float cosAngle, float sinAngle)
{
	float blockerSum = 0.0;
	int blockerCount = 0;

	for (int i = 0; i < BLOCKER_SEARCH_SAMPLES; ++i)
	{
		vec2 rotated = RotateOffset(poissonDisk[i], cosAngle, sinAngle);
		vec2 offset = rotated * searchRadius * texelSize;
		float shadowMapDepth = texture(shadowMap,
			vec3(shadowCoord.xy + offset, float(cascadeIndex))).r;

		if (shadowMapDepth < shadowCoord.z)
		{
			blockerSum += shadowMapDepth;
			blockerCount++;
		}
	}

	if (blockerCount == 0)
		return -1.0;

	return blockerSum / float(blockerCount);
}

// Step 2: Estimate penumbra width based on blocker distance
float EstimatePenumbraWidth(float receiverDepth, float blockerDepth, float lightSize)
{
	return lightSize * (receiverDepth - blockerDepth) / blockerDepth;
}

// Step 3: PCF with variable filter radius and per-fragment rotation
float PCF_Filter(sampler2DArray shadowMap, vec3 shadowCoord, uint cascadeIndex,
	float filterRadius, vec2 texelSize, float cosAngle, float sinAngle)
{
	float visibility = 0.0;

	for (int i = 0; i < PCF_SAMPLES; ++i)
	{
		vec2 rotated = RotateOffset(poissonDisk[i], cosAngle, sinAngle);
		vec2 offset = rotated * filterRadius * texelSize;
		float closestDepth = texture(shadowMap,
			vec3(shadowCoord.xy + offset, float(cascadeIndex))).r;
		visibility += (shadowCoord.z <= closestDepth) ? 1.0 : 0.0;
	}

	return visibility / float(PCF_SAMPLES);
}

// PCSS for a single cascade
float ShadowCalculationForCascade(sampler2DArray shadowMap, mat4 viewProjection,
	float lightSize, float minFilterRadius, float maxFilterRadius,
	uint cascadeIndex, vec3 worldPos, float cosAngle, float sinAngle)
{
	vec4 shadowCoord = viewProjection * vec4(worldPos, 1.0);
	shadowCoord.xyz /= shadowCoord.w;
	shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;

	if (shadowCoord.z > 1.0)
		return 1.0;

	vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0).xy);

	// Sqrt scale: smooth penumbra transition between cascades
	float cascadeScale = sqrt(float(cascadeIndex + 1));
	float scaledLightSize = lightSize * cascadeScale;

	// Step 1: Blocker search
	float searchRadius = scaledLightSize * 20.0;
	float avgBlockerDepth = FindBlockerDepth(shadowMap, shadowCoord.xyz, cascadeIndex,
		searchRadius, texelSize, cosAngle, sinAngle);

	if (avgBlockerDepth < 0.0)
		return 1.0;

	// Step 2: Penumbra estimation
	float penumbraWidth = EstimatePenumbraWidth(shadowCoord.z, avgBlockerDepth, scaledLightSize);

	// Step 3: PCF
	float filterRadius = penumbraWidth * float(textureSize(shadowMap, 0).x);
	filterRadius = clamp(filterRadius, minFilterRadius, maxFilterRadius);

	return PCF_Filter(shadowMap, shadowCoord.xyz, cascadeIndex,
		filterRadius, texelSize, cosAngle, sinAngle);
}

// PCSS Shadow Calculation with cascade blending
float ShadowCalculation(sampler2DArray shadowMap, mat4 viewProjection[SHADOW_MAP_CASCADE_COUNT],
	float splitDepth[SHADOW_MAP_CASCADE_COUNT], uint cascadeCount,
	float lightSize, float minFilterRadius, float maxFilterRadius,
	float cascadeBlendFactor,
	vec3 worldPos, float viewDepth)
{
	// Select cascade based on view-space depth
	uint cascadeIndex = 0;
	for (uint i = 0; i < cascadeCount - 1; ++i)
	{
		if (viewDepth < splitDepth[i])
		{
			cascadeIndex = i + 1;
		}
	}

	// Per-fragment rotation
	float noise = InterleavedGradientNoise(gl_FragCoord.xy);
	float angle = noise * 6.283185;
	float cosAngle = cos(angle);
	float sinAngle = sin(angle);

	// Calculate visibility for current cascade
	float visibility = ShadowCalculationForCascade(shadowMap, viewProjection[cascadeIndex],
		lightSize, minFilterRadius, maxFilterRadius,
		cascadeIndex, worldPos, cosAngle, sinAngle);

	// Blend with next cascade near the split boundary
	// splitDepth and viewDepth are negative (view space convention):
	//   viewDepth = -15.0, splitDepth[0] = -30.0
	//   As viewDepth approaches splitDepth from above (less negative ¡æ more negative),
	//   distToSplit goes from positive to zero
	if (cascadeBlendFactor > 0.0 && cascadeIndex < cascadeCount - 1)
	{
		float currentSplit = splitDepth[cascadeIndex];
		float prevSplit = (cascadeIndex > 0u) ? splitDepth[cascadeIndex - 1] : 0.0;

		// Both values are negative, so abs gives the positive range
		float cascadeRange = abs(currentSplit - prevSplit);
		float blendRegion = cascadeRange * cascadeBlendFactor;

		// Distance from fragment to next cascade boundary (positive when inside current cascade)
		float distToSplit = abs(viewDepth - currentSplit);

		if (distToSplit < blendRegion)
		{
			// 1.0 at split boundary ¡æ 0.0 at blend region end
			float blendFactor = 1.0 - smoothstep(0.0, blendRegion, distToSplit);

			uint nextCascade = cascadeIndex + 1;
			float nextVisibility = ShadowCalculationForCascade(shadowMap, viewProjection[nextCascade],
				lightSize, minFilterRadius, maxFilterRadius,
				nextCascade, worldPos, cosAngle, sinAngle);

			visibility = mix(visibility, nextVisibility, blendFactor);
		}
	}

	return visibility;
}

// SDF Shadow sampling utility for fragment shaders
// Used when SDF shadow map is pre-computed via compute shader

// Sample the pre-computed SDF shadow map
float SampleSDFShadow(sampler2D sdfShadowMap, vec2 screenUV)
{
    return texture(sdfShadowMap, screenUV).r;
}

// Combine CSM shadow with SDF shadow
// CSM handles close-range detail, SDF provides long-range soft shadows
float CombineShadows(float csmShadow, float sdfShadow, float blendFactor)
{
    // blendFactor: 0.0 = CSM only, 1.0 = SDF only
    return mix(csmShadow, min(csmShadow, sdfShadow), blendFactor);
}