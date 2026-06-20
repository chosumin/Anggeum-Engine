/* Copyright (c) 2020, Arm Limited and Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License";
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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