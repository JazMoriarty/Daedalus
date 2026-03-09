// transparent.metal
// Forward PBR pass for alpha-blended transparent geometry (spec §Pass 11).
//
// This shader is used for AlphaMode::Blended billboard sprites and any other
// geometry that needs per-pixel alpha blending rather than hard cutout.
//
// Per-draw resources:
//   texture(0)  = albedo map     (RGBA8, sRGB or linear)
//   texture(1)  = normal map     (RGBA8, tangent-space)
//   texture(2)  = shadow depth   (Depth32Float, read as float for bias compare)
//   texture(3)  = emissive map   (RGBA8 sRGB or RGBA16F; nil fallback → black)
//   sampler(0)  = linear-repeat  (material textures)
//   sampler(1)  = linear-clamp   (shadow depth lookup)
//   buffer(0)   = FrameConstants  (vertex + fragment)
//   buffer(1)   = ModelConstants  (vertex only)
//   buffer(1)   = MaterialConstants (fragment only — includes roughness, metalness, tint)
//   buffer(3)   = SpotLightGPU    (fragment only)
//
// Lighting model:
//   Final HDR colour = ambient + Cook-Torrance spot light contribution + emissive.
//   Emissive is added unconditionally (not scaled by NdotL), making sprites that
//   set emissiveTexture visible regardless of scene light direction.
//   Shadow: single-sample bias test against the shadow atlas (same as lighting.metal).
//   No IBL — not yet available in Phase 1D.  The ambient term uses scene.ambientColor.
//
// Alpha:
//   output.a = albedoSample.a * mat.tint.a
//   The PSO blend state is SrcAlpha / OneMinusSrcAlpha on the HDR render target.
//
// TAA limitation (Phase 1D):
//   Transparent objects do not write to the G-buffer motion vector attachment.
//   TAA reprojection uses zero motion for transparent pixels, which may produce
//   minor ghosting for fast-moving blended sprites.  This will be resolved in a
//   later phase when transparent objects write their own motion vectors.

#include "common.h"

// ─── Vertex ───────────────────────────────────────────────────────────────────

struct TransVertIn
{
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
    float4 tangent  [[attribute(3)]];  // xyz = tangent, w = handedness (+1 or -1)
};

struct TransVertOut
{
    float4 position     [[position]];
    float3 worldPos;
    float3 worldNormal;
    float3 worldTangent;
    float3 worldBitangent;
    float2 uv;
};

vertex TransVertOut transparent_vert(
    TransVertIn              in    [[stage_in]],
    constant FrameConstants& frame [[buffer(0)]],
    constant ModelConstants& model [[buffer(1)]])
{
    TransVertOut out;

    float4 worldPos4 = model.model * float4(in.position, 1.0);
    out.worldPos     = worldPos4.xyz;
    out.position     = frame.viewProj * worldPos4;

    // Transform normal and tangent to world space via the normal matrix.
    float3 N = normalize((model.normalMat * float4(in.normal,      0.0)).xyz);
    float3 T = normalize((model.normalMat * float4(in.tangent.xyz, 0.0)).xyz);
    // Re-orthogonalise T against N (Gram-Schmidt).
    T = normalize(T - dot(T, N) * N);
    // Compute bitangent; handedness is stored in tangent.w.
    float3 B = cross(N, T) * in.tangent.w;

    out.worldNormal    = N;
    out.worldTangent   = T;
    out.worldBitangent = B;
    out.uv             = in.uv;
    return out;
}

// ─── Fragment ─────────────────────────────────────────────────────────────────

fragment float4 transparent_frag(
    TransVertOut                   in          [[stage_in]],
    texture2d<float>               albedoTex   [[texture(0)]],
    texture2d<float>               normalTex   [[texture(1)]],
    texture2d<float>               shadowTex   [[texture(2)]],
    texture2d<float>               emissiveTex [[texture(3)]],
    sampler                        repeatSamp  [[sampler(0)]],
    sampler                        clampSamp   [[sampler(1)]],
    constant FrameConstants&       frame       [[buffer(0)]],
    constant MaterialConstants&    mat         [[buffer(1)]],
    constant uint&                 spotCount   [[buffer(3)]],
    constant SpotLightGPU*         spotLights  [[buffer(4)]])
{
    // ─── Sprite sheet UV crop ────────────────────────────────────────────────
    const float2 uv = in.uv * mat.uvScale + mat.uvOffset;

    // ─── Sample albedo + apply tint ───────────────────────────────────────────────
    const float4 albedoSample = albedoTex.sample(repeatSamp, uv);
    const float3 albedo       = albedoSample.rgb * mat.tint.rgb;
    const float  alpha        = albedoSample.a   * mat.tint.a;

    // ─── World-space normal from normal map ─────────────────────────────────────────
    const float3 tNormal  = normalTex.sample(repeatSamp, uv).xyz * 2.0 - 1.0;
    const float3x3 TBN    = float3x3(in.worldTangent, in.worldBitangent, in.worldNormal);
    const float3 N        = normalize(TBN * tNormal);
    const float3 V        = normalize(frame.cameraPos.xyz - in.worldPos);

    // ─── Ambient term ─────────────────────────────────────────────────────────
    const float3 ambient = frame.ambientColor.rgb * albedo;

    // ─── Spot lights ───────────────────────────────────────────────────────────
    // All spots contribute; only index 0 (shadow-caster) uses the shadow map.
    float3 spotContrib = float3(0.0);
    for (uint si = 0; si < spotCount; ++si)
    {
        const SpotLightGPU spot = spotLights[si];
        if (spot.colorIntensity.w <= 0.0f) continue;

        const float3 toLight = spot.positionRange.xyz - in.worldPos;
        const float3 L       = normalize(toLight);
        const float  dist    = length(toLight);
        const float  range   = spot.positionRange.w;

        const float attenuation =
            saturate(1.0 - (dist / range)) / (dist * dist + 1.0);

        const float spotCos   = dot(-L, normalize(spot.directionOuterCos.xyz));
        const float outerCos  = spot.directionOuterCos.w;
        const float innerCos  = spot.innerCosAndPad.x;
        const float coneAtten = saturate((spotCos - outerCos) /
                                         (innerCos - outerCos + 0.0001));

        // Shadow: only the first (shadow-casting) spotlight uses the shadow map.
        float shadow = 1.0;
        if (si == 0)
        {
            const float4 shadowClip   = frame.sunViewProj * float4(in.worldPos, 1.0);
            const float3 shadowNDC    = shadowClip.xyz / shadowClip.w;
            const float2 shadowUV     = shadowNDC.xy * float2(0.5, -0.5) + 0.5;
            const float  surfaceDepth = shadowNDC.z;
            if (all(shadowUV >= 0.0) && all(shadowUV <= 1.0))
            {
                const float storedDepth = shadowTex.sample(clampSamp, shadowUV).r;
                shadow = (surfaceDepth - 0.002 <= storedDepth) ? 1.0 : 0.0;
            }
        }

        const float3 lightColor = spot.colorIntensity.rgb * spot.colorIntensity.w;
        spotContrib += cook_torrance(N, V, L, albedo, mat.roughness, mat.metalness)
                     * lightColor * attenuation * coneAtten * shadow;
    }

    // ─── Emissive (additive; self-illumination for sprites with NdotL ≈ 0) ──────
    // Sampled with the same UV crop as albedo so animated sprite frames align.
    // When emissiveTexture is not set, the fallback 1×1 black texture is bound
    // and the contribution is zero.
    const float3 emissive = emissiveTex.sample(repeatSamp, uv).rgb;

    // ─── Combine and output ───────────────────────────────────────────────────
    const float3 hdrColor = ambient + spotContrib + emissive;
    return float4(hdrColor, alpha);
}
