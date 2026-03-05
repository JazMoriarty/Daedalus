// lighting.metal
// Deferred PBR lighting pass (compute).
//
// Inputs:
//   texture(0) = gAlbedoRoughness  RGBA8Unorm  (read)
//   texture(1) = gNormalMetal      RGBA16Float (read, xyz=normal, a=metalness)
//   texture(2) = gDepth            Depth32Float (read)
//   texture(3) = ssaoTex           R16Float    (read)
//   texture(4) = hdrOut            RGBA16Float (write)
//   texture(5) = shadowMap         Depth32Float (read, manual PCF compare)
//   buffer(0)  = FrameConstants
//   buffer(1)  = point light count (u32)
//   buffer(2)  = PointLightGPU[]
//   buffer(3)  = SpotLightGPU  (first shadow-casting spot light; zero intensity = inactive)

#include "common.h"

kernel void lighting_main(
    texture2d<float, access::read>   gAlbedoRoughness [[texture(0)]],
    texture2d<float, access::read>   gNormalMetal     [[texture(1)]],
    texture2d<float, access::read>   gDepth           [[texture(2)]],
    texture2d<float, access::read>   ssaoTex          [[texture(3)]],
    texture2d<float, access::write>  hdrOut           [[texture(4)]],
    texture2d<float, access::read>   shadowMap        [[texture(5)]],
    constant FrameConstants&         frame            [[buffer(0)]],
    constant uint&                   lightCount       [[buffer(1)]],
    constant PointLightGPU*          lights           [[buffer(2)]],
    constant SpotLightGPU&           spotLight        [[buffer(3)]],
    uint2                            gid              [[thread_position_in_grid]])
{
    const uint fw = hdrOut.get_width();
    const uint fh = hdrOut.get_height();
    if (gid.x >= fw || gid.y >= fh) return;

    float2 uv    = (float2(gid) + 0.5f) / float2(fw, fh);
    float  depth = gDepth.read(gid).r;

    // Sky: leave black — skybox pass fills this in
    if (depth >= 0.9999f)
    {
        hdrOut.write(float4(0, 0, 0, 0), gid);
        return;
    }

    // ─── Unpack G-buffer ──────────────────────────────────────────────────────
    float4 albedoR  = gAlbedoRoughness.read(gid);
    float4 normalM  = gNormalMetal.read(gid);
    float  ao       = ssaoTex.read(gid).r;

    float3 albedo    = albedoR.rgb;
    float  roughness = albedoR.a;
    float3 N         = normalize(normalM.xyz);
    float  metalness = normalM.a;

    float3 worldPos = reconstruct_world_pos(depth, uv, frame.invViewProj);
    float3 V        = normalize(frame.cameraPos.xyz - worldPos);

    // ─── Directional sun light (no shadow — sun is blocked by walls for interior;
    //     for outdoor scenes sunIntensity > 0 and no spot light is present) ───
    float3 sunDir = normalize(frame.sunDirection.xyz);
    float3 sunCol = frame.sunColor.xyz * frame.sunColor.w;  // colour × intensity
    float3 radiance = cook_torrance(N, V, sunDir, albedo, roughness, metalness) * sunCol;

    // ─── Spot light with PCF shadow (3×3 kernel) ────────────────────────────
    // Active when spotLight.colorIntensity.w > 0 (intensity is the gate).
    if (spotLight.colorIntensity.w > 0.0f)
    {
        float3 toLight = spotLight.positionRange.xyz - worldPos;
        float  dist    = length(toLight);
        float  range   = spotLight.positionRange.w;

        if (dist < range)
        {
            float3 L          = toLight / dist;
            float3 spotDir    = normalize(spotLight.directionOuterCos.xyz);
            float  cosTheta   = dot(-L, spotDir);
            float  innerCos   = spotLight.innerCosAndPad.x;
            float  outerCos   = spotLight.directionOuterCos.w;
            float  cone       = smoothstep(outerCos, innerCos, cosTheta);
            float  atten      = 1.0f - clamp(dist / range, 0.0f, 1.0f);
            atten *= atten;

            // PCF shadow — reuses frame.sunViewProj (written with spot matrix)
            float4 shadowClip = frame.sunViewProj * float4(worldPos, 1.0);
            float3 shadowNDC  = shadowClip.xyz / shadowClip.w;
            float2 shadowUV   = ndc_to_uv(shadowNDC.xy);
            float  refDepth   = shadowNDC.z - 0.002f;  // bias (near=0.5 gives good precision, small bias keeps shadow tight)
            float  lit        = 1.0f;
            if (shadowUV.x >= 0.0f && shadowUV.x <= 1.0f &&
                shadowUV.y >= 0.0f && shadowUV.y <= 1.0f &&
                shadowNDC.z >= 0.0f && shadowNDC.z <= 1.0f)
            {
                lit = 0.0f;
                float2 shadowPx = shadowUV * 2048.0f;
                for (int dx = -1; dx <= 1; dx++)
                    for (int dy = -1; dy <= 1; dy++)
                    {
                        uint2 tc = uint2(clamp(shadowPx + float2(dx, dy),
                                               float2(0.0f), float2(2047.0f)));
                        float stored = shadowMap.read(tc).r;
                        lit += (stored >= refDepth) ? 1.0f : 0.0f;
                    }
                lit /= 9.0f;
            }

            radiance += cook_torrance(N, V, L, albedo, roughness, metalness)
                        * spotLight.colorIntensity.xyz * spotLight.colorIntensity.w
                        * cone * atten * lit;
        }
    }

    // ─── Point lights ─────────────────────────────────────────────────────────
    for (uint i = 0; i < lightCount; ++i)
    {
        float3 lPos   = lights[i].positionRadius.xyz;
        float  lRad   = lights[i].positionRadius.w;
        float3 lCol   = lights[i].colorIntensity.xyz;
        float  lIntens = lights[i].colorIntensity.w;

        float3 L      = lPos - worldPos;
        float  dist   = length(L);
        if (dist > lRad) continue;

        L /= dist;
        float attenuation = clamp(1.0f - (dist / lRad), 0.0f, 1.0f);
        attenuation *= attenuation;

        radiance += cook_torrance(N, V, L, albedo, roughness, metalness)
                    * lCol * lIntens * attenuation;
    }

    // ─── Ambient (AO-modulated) ───────────────────────────────────────────────
    float3 ambient = frame.ambientColor.xyz * albedo * ao;

    float3 colour = radiance + ambient;
    hdrOut.write(float4(colour, 1.0f), gid);
}
