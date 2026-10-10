#include "pbr.glsl"

// Light data shared with the CPU (BufferObjects.h) and the tile light
// culling, plus the forward shading every lit surface uses: the per-light
// response, the Cook-Torrance direct term and the split-sum IBL ambient term.
// Include after common.glsl (PI); pulls in pbr.glsl itself.

#define DIRECTIONAL_LIGHT 0
#define POINT_LIGHT 1
#define SPOT_LIGHT 2

#define MAX_FORWARD_LIGHT_COUNT 1000

#define TILE_SIZE 16
#define MAX_POINT_LIGHT_PER_TILE 128

struct LightVisiblity
{
	uint count;
	uint lightIndices[MAX_POINT_LIGHT_PER_TILE];
};

struct Light
{
	vec4 position;         // position.w represents type of light
	vec4 color;            // color.w represents light intensity
	vec4 direction;        // direction.w = range
	vec2 info;             // (only used for spot lights) info.x represents light inner cone angle, info.y represents light outer cone angle
};

vec3 ApplyDirectionalLight(Light light, vec3 normal)
{
	vec3 worldToLight = normalize(light.direction.xyz);
	float ndotl = clamp(dot(normal, worldToLight), 0.0, 1.0);
	return ndotl * light.color.w * light.color.rgb;
}

vec3 ApplyPointLight(Light light, vec3 pos, vec3 normal)
{
	vec3 worldToLight = light.position.xyz - pos;
	float dist = length(worldToLight);
	float radius = light.direction.w; // Using w as the maximum light influence radius

	// 1. Distance Attenuation (Standard inverse square law)
	if (dist > radius) return vec3(0.0);

	float atten = 1.0 / (max(dist * dist, 0.01));

	// 2. Window function to smoothly fade out at the radius boundary
	// Prevents tiling artifacts caused by sudden culling
	float factor = dist / radius;
	float window = clamp(1.0 - factor * factor * factor * factor, 0.0, 1.0);
	atten *= (window * window);

	// 3. Diffuse lighting calculation (Lambertian)
	worldToLight = normalize(worldToLight);
	float ndotl = clamp(dot(normal, worldToLight), 0.0, 1.0);

	// Final result: Color * Intensity * Diffuse * Attenuation
	return light.color.rgb * (light.color.w * ndotl * atten);
}

vec3 ApplySpotLight(Light light, vec3 pos, vec3 normal)
{
	vec3 worldToLight = light.position.xyz - pos;
	float dist = length(worldToLight);
	float radius = light.direction.w; // Radius matches the culling bounding box

	// 1. Distance Attenuation (Same logic as Point Light for consistency)
	if (dist > radius) return vec3(0.0);

	float atten = 1.0 / (max(dist * dist, 0.01));
	float factor = dist / radius;
	float window = clamp(1.0 - factor * factor * factor * factor, 0.0, 1.0);
	atten *= (window * window);

	// 2. Cone Attenuation (Angular falloff)
	worldToLight = normalize(worldToLight);
	// -direction.xyz = cone forward (travel direction), direction.xyz = worldToLight-equivalent
	float cosTheta = dot(worldToLight, normalize(-light.direction.xyz));

	float innerCos = light.info.x; // cos(innerAngle) passed from CPU
	float outerCos = light.info.y; // cos(outerAngle) passed from CPU

	// Calculate intensity based on the angle between inner and outer cones
	float angleAttenuation = clamp((cosTheta - outerCos) / (innerCos - outerCos), 0.0, 1.0);
	angleAttenuation = smoothstep(0.0, 1.0, angleAttenuation);

	// 3. Diffuse lighting calculation
	float ndotl = clamp(dot(normal, worldToLight), 0.0, 1.0);

	return light.color.rgb * (light.color.w * ndotl * atten * angleAttenuation);
}

vec3 GetLightDirection(Light light, vec3 worldPos)
{
	if (light.position.w == DIRECTIONAL_LIGHT)
	{
		return light.direction.xyz;
	}
	else
	{
		return light.position.xyz - worldPos;
	}
}

vec3 ApplyLight(Light light, vec3 pos, vec3 normal)
{
	if (light.position.w == DIRECTIONAL_LIGHT)
	{
		return ApplyDirectionalLight(light, normal);
	}
	else if (light.position.w == POINT_LIGHT)
	{
		return ApplyPointLight(light, pos, normal);
	}
	else
		return ApplySpotLight(light, pos, normal);
}

struct Surface
{
    vec3 albedo;
    float metallic;
    float roughness;
    vec3 N;
    vec3 V;
    vec3 F0;
};

Surface MakeSurface(vec3 albedo, float metallic, float roughness, vec3 N, vec3 V)
{
    Surface s;
    s.albedo = albedo;
    s.metallic = metallic;
    s.roughness = roughness;
    s.N = N;
    s.V = V;
    s.F0 = mix(vec3(0.04), albedo, metallic);
    return s;
}

// Radiance from one light, unshadowed.
vec3 DirectLighting(Surface s, Light light, vec3 worldPos)
{
    vec3 L = normalize(GetLightDirection(light, worldPos));
    vec3 H = normalize(s.V + L);
    vec3 radiance = ApplyLight(light, worldPos, s.N);

    float NDF = DistributionGGX(s.N, H, s.roughness);
    float G = GeometrySmith(s.N, s.V, L, s.roughness);
    vec3 F = FresnelSchlick(max(dot(H, s.V), 0.0), s.F0);

    vec3 kD = (1.0 - F) * (1.0 - s.metallic);
    float denominator = 4.0 * max(dot(s.N, s.V), 0.0) * max(dot(s.N, L), 0.0) + 0.0001;
    vec3 specular = NDF * G * F / denominator;

    return (kD * s.albedo / PI + specular) * radiance;
}

// Split-sum IBL. ao darkens the diffuse part, specularOcclusion the specular.
vec3 AmbientLighting(Surface s, samplerCube irradianceMap, samplerCube prefilterMap,
    sampler2D brdfLut, float ao, float specularOcclusion)
{
    float NdotV = max(dot(s.N, s.V), 0.0);
    vec3 kS = FresnelSchlick(NdotV, s.F0);
    vec3 kD = (1.0 - kS) * (1.0 - s.metallic);

    vec3 diffuse = texture(irradianceMap, s.N).rgb * s.albedo;

    const float MAX_REFLECTION_LOD = 4.0;
    vec3 R = reflect(-s.V, s.N);
    vec3 prefilteredColor = textureLod(prefilterMap, R, s.roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLut, vec2(NdotV, s.roughness)).rg;
    vec3 specular = prefilteredColor * (brdf.x * kS + brdf.y) * specularOcclusion;

    // Global factor keeping the ambient term below the direct one.
    const float AMBIENT_SCALE = 0.1;
    return (kD * diffuse * ao + specular) * AMBIENT_SCALE;
}

// Every forward shader tonemaps with the same exposure.
const float FORWARD_EXPOSURE = 4.5;
