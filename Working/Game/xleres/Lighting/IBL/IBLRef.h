// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(IBL_REF_H)
#define IBL_REF_H

#include "IBLAlgorithm.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
    //  Reference specular reflections
///////////////////////////////////////////////////////////////////////////////////////////////////

float3 SampleSpecularIBL_Ref(float3 normal, float3 viewDirection, SpecularParameters specParam, TextureCube tex)
{
        // hack -- currently problems when roughness is 0!
    if (specParam.roughness == 0.f) return 0.0.xxx;

    // This is a reference implementation of glossy specular reflections from a
    // cube map texture. It's too inefficient to run at real-time. But it provides
    // a visual reference.
    // In the typical final-result, we must use precalculated lookup tables to reduce
    // the number of samples down to 1.
    // We will sample a fixed number of directions across the peak area of the GGX
    // specular highlight. We return the mean of those sample directions.
    // As standard, we use a 2D Hammersley distribution to generate sample directions.
    //
    // For reference, see
    // GPU Gems article
    //      http://http.developer.nvidia.com/GPUGems3/gpugems3_ch20.html
    // Unreal course notes
    //      http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf

    float alphad = RoughnessToDAlpha(specParam.roughness);
    float3 result = 0.0.xxx;
    const uint sampleCount = 512;
    for (uint s=0; s<sampleCount; ++s) {
            // We could build a distribution of "H" vectors here,
            // or "L" vectors. It makes sense to use H vectors
        precise float3 H = BuildSampleHalfVectorGGX(s, sampleCount, normal, alphad);
        float3 L = 2.f * dot(viewDirection, H) * H - viewDirection;

            // Now we can light as if the point on the reflection map
            // is a directonal light

        float3 lightColor = tex.SampleLevel(DefaultSampler, AdjSkyCubeMapCoords(L), 0).rgb;
        precise float3 brdf = CalculateSpecular(normal, viewDirection, L, H, specParam); // (also contains NdotL term)

            // Unreal course notes say the probability distribution function is
            //      D * NdotH / (4 * VdotH)
            // We need to apply the inverse of this to weight the sample correctly.
            // A better solution is to factor the terms out of the microfacet specular
            // equation. But since this is for a reference implementation, let's do
            // it the long way.
        float NdotH = saturate(dot(normal, H));
        float VdotH = saturate(dot(viewDirection, H));
        precise float D = TrowReitzD(NdotH, alphad);
        float pdfWeight = (4.f * VdotH) / (D * NdotH);

        result += lightColor * brdf * pdfWeight;
    }

    return result / float(sampleCount);
}

float3 SampleTransmittedSpecularIBL_Ref(
    float3 normal, float3 viewDirection,
    SpecularParameters specParam, TextureCube tex)
{
        // hack -- currently problems when roughness is 0!
    if (specParam.roughness == 0.f) return 0.0.xxx;

    float iorIncident = 1.f;
    float iorOutgoing = SpecularTransmissionIndexOfRefraction;
    float3 ot = viewDirection;

        // This is a reference implementation of transmitted IBL specular.
        // We're going to follow the same method and microfacet distribution as
        // SampleSpecularIBL_Ref
    float alphad = RoughnessToDAlpha(specParam.roughness);
    float3 result = 0.0.xxx;
    const uint sampleCount = 128;
    for (uint s=0; s<sampleCount; ++s) {
        // using the same distribution of half-vectors that we use for reflection
        // (except we flip the normal because of the way the equation is built)
        precise float3 H = BuildSampleHalfVectorGGX(s, sampleCount, -normal, alphad);

        // following Walter07 here, we need to build "i", the incoming direction.
        // Actually Walter builds the outgoing direction -- but we need to reverse the
        // equation and get the incoming direction.
        float3 i;
        if (!CalculateTransmissionIncident(i, ot, H, iorIncident, iorOutgoing))
            continue;

        #if 0
            if (isinf(i.x) || isnan(i.x)) return float3(1,0,0);
            if (abs(length(i) - 1.f) > 0.01f) return float3(1,0,0);
            float3 H2 = CalculateHt(i, ot, iorIncident, iorOutgoing);
            if (length(H2 - H) > 0.01f) return float3(1,0,0);
        #endif

        // ok, we've got our incoming vector. We can do the cube map lookup
        // Note that when we call "CalculateSpecular", it's going to recalculate
        // the transmission half vector and come out with the same result.
        float3 lightColor = tex.SampleLevel(DefaultSampler, AdjSkyCubeMapCoords(i), 0).rgb;
        precise float3 brdf = CalculateSpecular(normal, viewDirection, i, H, specParam); // (also contains NdotL term)

        // We have to apply the distribution weight. Since our half vectors are distributed
        // in the same fashion as the reflection case, we should have the same weight.
        // (But we need to consider the flipping that occurs)
        // float NdotH = saturate(dot(normal, -H));
        // float VdotH = saturate(dot(viewDirection, -H));
        // precise float D = TrowReitzD(NdotH, alphad);
        // float pdfWeight = (4.f * VdotH) / (D * NdotH);
        float pdfWeight = SamplingPDFWeight(H, -normal, viewDirection, alphad);

        result += lightColor * brdf * pdfWeight;
    }

    return result / float(sampleCount);
}

#endif