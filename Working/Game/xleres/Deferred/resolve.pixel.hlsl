// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(RESOLVE_PSH)
#define RESOLVE_PSH

#include "resolveutil.hlsl"
#include "resolvecascade.hlsl"
#include "../TechniqueLibrary/Framework/CommonResources.hlsl"
#include "../TechniqueLibrary/Math/Misc.hlsl"
#include "../TechniqueLibrary/SceneEngine/Lighting/DirectionalResolve.hlsl"
#include "../TechniqueLibrary/SceneEngine/Lighting/ShadowsResolve.hlsl"
#include "../TechniqueLibrary/SceneEngine/Lighting/LightDesc.hlsl"
#include "../TechniqueLibrary/Utility/LoadGBuffer.hlsl"
#include "../TechniqueLibrary/Utility/Colour.hlsl"	// for LightingScale
#include "../TechniqueLibrary/Framework/Binding.hlsl"

#if HAS_SCREENSPACE_AO==1
    Texture2D<float>			AmbientOcclusion		: register(t5);
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////

float4 main(float4 position : SV_Position, SystemInputs sys, float2 texCoord : TEXCOORD0) : SV_Target0
{
	return float4(LoadGBuffer(position, sys).worldSpaceNormal.rgb, 1);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////
             //   s h a d o w s   //

float4 DebugShadows(float2 texCoord)
{
	float2 t = fmod(texCoord.xy*2.f, 1.0.xx);
	uint index = uint(texCoord.x/.5f) + 2*uint(texCoord.y/.5f);
	float shadowSample =
		ShadowTextures.SampleCmpLevelZero(
			ShadowSampler, float3(t.xy, float(index)),
			0.995f);
	return float4(shadowSample.xxx, 1.f);
}

float ResolvePointLightShadows(float3 worldPosition, int2 randomizerValue, uint msaaSampleIndex, ShadowResolveConfig config)
{
		// simplified version of ResolveShadows for point lights
	float4 frustumCoordinates;
	float2 texCoords;

		// "orthogonal" shadow projection method doesn't make sense for point light source
		// shadows. In this case, it must be arbitrary
	uint c=0;
	for (;;c++) {
		if (c == 6) {
			return 1.f;
		}

		frustumCoordinates = mul(ShadowWorldToProj[c], float4(worldPosition, 1));
		texCoords = frustumCoordinates.xy / frustumCoordinates.w;
		if (max(abs(texCoords.x), abs(texCoords.y)) < 1.f) {
			break;
		}
	}

	texCoords = float2(0.5f + 0.5f * texCoords.x, 0.5f - 0.5f * texCoords.y);
	float comparisonDistance = frustumCoordinates.z / frustumCoordinates.w;
	float biasedDepth = comparisonDistance - 0.0005f;
	float casterDistance = CalculateShadowCasterDistance(
		texCoords, comparisonDistance, c, msaaSampleIndex,
		DitherPatternValue(randomizerValue));

	float4 miniProj = ShadowProjection_GetMiniProj(c);
	if (!ShadowsPerspectiveProjection) {
			// In orthogonal projection mode, NDC depths are actually linear. So, we can convert a difference
			// of depths in NDC space (like casterDistance) into world space depth easily. Linear depth values
			// are more convenient for calculating the shadow filter radius
		casterDistance = -NDCDepthDifferenceToWorldSpace_Ortho(casterDistance,
			AsMiniProjZW(miniProj));
	}

	if (config._doFiltering) {
		return CalculateFilteredShadows(
			texCoords, biasedDepth, c, casterDistance, randomizerValue,
			miniProj.xy, msaaSampleIndex, config);
	} else {
		return TestShadow(texCoords, c, biasedDepth);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

cbuffer LightBuffer BIND_MAT_B1
{
	LightDesc Light;
}

[earlydepthstencil]
float4 ResolveLight(	float4 position : SV_Position,
						float2 texCoord : TEXCOORD0,
						float3 viewFrustumVector : VIEWFRUSTUMVECTOR,
						SystemInputs sys) : SV_Target0
{
	// return DebugShadows(texCoord);
	// #if MSAA_SAMPLES>1
	// 	return 1.0.xxxx;
	// #endif

	int2 pixelCoords = position.xy;
	float3 worldPosition = CalculateWorldPosition(pixelCoords, GetSampleIndex(sys), viewFrustumVector);

	// return float4(fmod(worldPosition/10,1.0.xxx),1);
	// float4 frustumCoordinates = mul(ShadowProjection[0], float4(worldPosition, 1));
	// return float4(frustumCoordinates.xyz/frustumCoordinates.w, 1.f);

	// {
	// 	float worldSpaceDepth = GetWorldSpaceDepth(pixelCoords, GetSampleIndex(sys));
	// 	float3 cameraCoordinate = float3(
	// 		( 2.f * texCoord.x - 1.f) / SysUniform_GetMinimalProjection().x,
	// 		(-2.f * texCoord.y + 1.f) / SysUniform_GetMinimalProjection().y,
	// 		-1.f) * (worldSpaceDepth * SysUniform_GetFarClip());
	// 	worldPosition = mul(CameraToWorld, float4(cameraCoordinate, 1)).xyz;
	// 	return float4(fmod(worldPosition/10,1.0.xxx),1);
	// }

		// note, we can use 2 modes for projection here:
		//	by world position:
		//		calculates the world space position of the current pixel,
		//		and then transforms that world space position into the
		//		shadow cascade coordinates
		//	by camera position:
		//		this transforms directly from the NDC coordinates of the
		//		current pixel into the camera frustum space.
		//
		//	In theory, by camera position might be a little more accurate,
		//	because it skips the world position stage. The function has been
		//	optimised for accuracy.
	int finalCascadeIndex;
	float4 cascadeNormCoords;
	const bool resolveByWorldPosition = false;
	if (resolveByWorldPosition) {
		FindCascade_FromWorldPosition(finalCascadeIndex, cascadeNormCoords, worldPosition);
	} else {
		FindCascade_CameraToShadowMethod(
			finalCascadeIndex, cascadeNormCoords,
			texCoord, GetWorldSpaceDepth(pixelCoords, GetSampleIndex(sys)));
	}

	float shadow = 1.f;		// area outside of all cascades is considered outside of shadow...
	if (finalCascadeIndex >= 0) {
		shadow = ResolveShadows_Cascade(
			finalCascadeIndex, cascadeNormCoords, pixelCoords, GetSampleIndex(sys),
			ShadowResolveConfig_Default());
	}

	GBufferValues sample = LoadGBuffer(position, sys);

	float screenSpaceOcclusion = 1.f;
	#if HAS_SCREENSPACE_AO==1
        screenSpaceOcclusion = LoadFloat1(AmbientOcclusion, pixelCoords, GetSampleIndex(sys));
    #endif

	float3 directionToEye = normalize(-viewFrustumVector);
	float3 diffuse = LightResolve_Diffuse(sample, directionToEye, Light.Position, Light);
	float3 specular = LightResolve_Specular(sample, directionToEye, Light.Position, Light, screenSpaceOcclusion);

	const float lightScale = LightingScale;
	return float4((lightScale*shadow)*(diffuse + specular), 1.f);
}

float ReciprocalMagnitude(float3 vec)
{
    // note -- is there a performance or accuracy advantage to doing it this way?
    return rsqrt(dot(vec, vec));
}

float3 RepresentativeVector_Sphere(float3 lightCenter, float lightRadius, float3 litPoint, float3 reflectionDir)
{
    // We want to find the "representative point" for a spherical light source
    // This is the point on the object that best represents the integral of all
    // incoming light. For a sphere, this is easy.. We just want to find the
    // point on the sphere closest to the reflection ray. This works so long as the
    // sphere is not (partially) below the equator. But we'll ignore the artefacts in
    // these cases.
    // See Brian Karis' 2013 Unreal course notes for more detail.
    // See also GPU Gems 5 for Drobot's research on this topic.
    // See also interesting shadertoy. "Distance Estimated Area Lights"
    //      https://www.shadertoy.com/view/4ss3Ws

    float3 L = lightCenter - litPoint;
    float3 testPt = dot(reflectionDir, L) * reflectionDir;
    return lerp(L, testPt, saturate(lightRadius * ReciprocalMagnitude(testPt - L)));
}

[earlydepthstencil]
float4 ResolveAreaLight(	float4 position : SV_Position,
							float2 texCoord : TEXCOORD0,
							float3 viewFrustumVector : VIEWFRUSTUMVECTOR,
							SystemInputs sys) : SV_Target0
{
	// return DebugShadows(texCoord);
	// return float4(0.0.xxx,1);

	int2 pixelCoords = position.xy;
	float3 worldPosition = CalculateWorldPosition(pixelCoords, GetSampleIndex(sys), viewFrustumVector);
	float shadow = ResolvePointLightShadows(worldPosition, pixelCoords, GetSampleIndex(sys), ShadowResolveConfig_Default());
	GBufferValues sample = LoadGBuffer(position, sys);

    float screenSpaceOcclusion = 1.f;
    #if HAS_SCREENSPACE_AO==1
        screenSpaceOcclusion = LoadFloat1(AmbientOcclusion, pixelCoords, GetSampleIndex(sys));
    #endif

    float3 directionToEye = normalize(-viewFrustumVector);
    float3 reflectionDir = reflect(-directionToEye, sample.worldSpaceNormal);
    float3 lightNegDir = RepresentativeVector_Sphere(Light.Position, Light.SourceRadiusX, worldPosition, reflectionDir);
    float distanceSq = dot(lightNegDir, lightNegDir);
    lightNegDir *= rsqrt(distanceSq);

    float3 diffuse = LightResolve_Diffuse(sample, directionToEye, lightNegDir, Light);
    float3 specular = LightResolve_Specular(sample, directionToEye, lightNegDir, Light, screenSpaceOcclusion);

    float alpha = sample.material.roughness * sample.material.roughness;
    float alphaPrime = saturate(alpha + Light.SourceRadiusX * Light.SourceRadiusX / (2.f * distanceSq));
    float specAttenuation = (alpha * alpha) / (alphaPrime * alphaPrime);

    float distanceAttenuation = saturate(DistanceAttenuation(distanceSq, 1.f));
    float radiusDropOff = CalculateRadiusLimitAttenuation(distanceSq, Light.CutoffRange);

    const float lightScale = LightingScale;
    return float4((lightScale*radiusDropOff*distanceAttenuation)*(diffuse + specAttenuation * specular), 1.f);
}

#endif
