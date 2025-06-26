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
	vec4 direction;        // direction.w represents range
	vec2 info;             // (only used for spot lights) info.x represents light inner cone angle, info.y represents light outer cone angle
};

vec3 ApplyDirectionalLight(Light light, vec3 normal)
{
	vec3 worldToLight = -light.direction.xyz;
	worldToLight = normalize(worldToLight);
	float ndotl = clamp(dot(normal, worldToLight), 0.0, 1.0);
	return ndotl * light.color.w * light.color.rgb;
}

vec3 ApplyPointLight(Light light, vec3 pos, vec3 normal)
{
	vec3  worldToLight = light.position.xyz - pos;
	float dist = length(worldToLight);
	float atten = light.direction.w / (dist * dist);
	worldToLight = normalize(worldToLight);
	float ndotl = clamp(dot(normal, worldToLight), 0.0, 1.0);
	return ndotl * light.color.w * atten * light.color.rgb;
}

vec3 ApplySpotLight(Light light, vec3 pos, vec3 normal)
{
	vec3  lightToPixel = normalize(pos - light.position.xyz);
	float theta = dot(lightToPixel, normalize(light.direction.xyz));
	float innerConeAngle = light.info.x;
	float outerConeAngle = light.info.y;
	float intensity = (theta - outerConeAngle) / (innerConeAngle - outerConeAngle);
	return smoothstep(0.0, 1.0, intensity) * light.color.w * light.color.rgb;
}

vec3 GetLightDirection(Light light, vec3 worldPos)
{
	if (light.position.w == DIRECTIONAL_LIGHT)
	{
		return -light.direction.xyz;
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