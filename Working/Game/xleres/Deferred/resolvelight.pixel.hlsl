// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(RESOLVE_UNSHADOWED_PSH)
#define RESOLVE_UNSHADOWED_PSH

#include "../TechniqueLibrary/SceneEngine/Lighting/ResolverInterface.hlsl"
#include "../TechniqueLibrary/SceneEngine/Lighting/LightShapes.hlsl"
#include "../TechniqueLibrary/SceneEngine/Lighting/ShadowTypes.hlsl"
#include "../TechniqueLibrary/Utility/LoadGBuffer.hlsl"
#include "../TechniqueLibrary/Utility/Colour.hlsl" // for LightingScale
#include "../TechniqueLibrary/Framework/Binding.hlsl"
#include "resolveutil.hlsl"

#if HAS_SCREENSPACE_AO==1
    Texture2D<float>	AmbientOcclusion : register(t5);
#endif

cbuffer LightBuffer BIND_MAT_B1
{
	LightDesc Light;
}

#if LIGHT_RESOLVE_DYN_LINKING != 0
    ILightResolver      MainResolver;
    ICascadeResolver    MainCascadeResolver;
    IShadowResolver     MainShadowResolver;

    ILightResolver      GetLightResolver()      { return MainResolver; }
    ICascadeResolver    GetCascadeResolver()    { return MainCascadeResolver; }
    IShadowResolver     GetShadowResolver()     { return MainShadowResolver; }
#else

    // When dynamic linking is disabled, we select the correct interface
    // by using defines.
    // In debug mode (ie, when shader optimizations are disabled), this can
    // reduce performance (relative to calling plain functions)
    // But when the shaders are compiled, it seems the overhead is minimal.
    // Using the "interfaces" like this helps give structure to the code
    // (even though we're not actually using the dynamic resolve stuff), and
    // it gives us a few more options.

    ILightResolver      GetLightResolver()
    {
        #if LIGHT_SHAPE == 1
            Sphere result;
        #elif LIGHT_SHAPE == 2
            Tube result;
        #elif LIGHT_SHAPE == 3
            Rectangle result;
        #else
            Directional result;
        #endif
        return result;
    }

    ICascadeResolver    GetCascadeResolver()
    {
        #if SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ARBITRARY
            CascadeResolver_Arbitrary result;
        #elif SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ORTHOGONAL
            #if SHADOW_ENABLE_NEAR_CASCADE != 0
                CascadeResolver_OrthogonalWithNear result;
            #else
                CascadeResolver_Orthogonal result;
            #endif
        #else
            CascadeResolver_None result;
        #endif
        return result;
    }

    IShadowResolver     GetShadowResolver()
    {
        #if SHADOW_CASCADE_MODE == 0
            ShadowResolver_None result;
        #elif SHADOW_RESOLVE_MODEL == 0
            ShadowResolver_PoissonDisc result;
        #else
            ShadowResolver_Smooth result;
        #endif
        return result;
    }
#endif

[earlydepthstencil]
float4 main(
    float4 position : SV_Position,
	float2 texCoord : TEXCOORD0,
	float3 viewFrustumVector : VIEWFRUSTUMVECTOR,
	SystemInputs sys) : SV_Target0
{
	int2 pixelCoords = position.xy;
	GBufferValues sample = LoadGBuffer(position.xy, sys);

    LightSampleExtra sampleExtra;
    sampleExtra.screenSpaceOcclusion = 1.f;
	#if HAS_SCREENSPACE_AO==1
        sampleExtra.screenSpaceOcclusion = LoadFloat1(AmbientOcclusion, pixelCoords, GetSampleIndex(sys));
    #endif

    LightScreenDest screenDest;
    screenDest.pixelCoords = pixelCoords;
    screenDest.sampleIndex = GetSampleIndex(sys);

    // Note -- we could pre-multiply (miniProj.W/SysUniform_GetFarClip()) into the view frustum vector to optimise this slightly...?
    float worldSpaceDepth = GetWorldSpaceDepth(pixelCoords, GetSampleIndex(sys));
    float3 worldPosition = SysUniform_GetWorldSpaceView() + (worldSpaceDepth / SysUniform_GetFarClip()) * viewFrustumVector;

    float3 result = GetLightResolver().Resolve(
        sample, sampleExtra, Light, worldPosition,
        normalize(-viewFrustumVector), screenDest);

    // Also calculate the shadowing -- (though we could skip it if the lighting is too dim here)
    CascadeAddress cascade = GetCascadeResolver().Resolve(worldPosition, texCoord, worldSpaceDepth);
    float shadow = GetShadowResolver().Resolve(cascade, screenDest);

	return float4((LightingScale*shadow)*result, 1.f);
}

#endif
