// gbuffer.metal
// Geometry pass: fills the G-buffer with surface properties.
//
// G-buffer layout (spec §Pass 4):
//   RT0 (RGBA8Unorm)  : albedo.rgb + baked_AO.a  (AO defaults to 1.0 until SSAO is wired)
//   RT1 (RGBA8Unorm)  : octahedral_normal.rg + roughness.b + metalness.a
//   RT2 (RGBA16Float) : emissive.rgb  (a unused)
//   RT3 (RG16Float)   : motion vectors (UV-delta space)
//
// Depth attachment: Depth32Float (written by hardware).
//
// Per-draw resources:
//   texture(0) = albedo map     (RGBA8, sRGB or linear)
//   texture(1) = normal map     (RGBA8, tangent-space)
//   texture(2) = emissive map   (RGBA8, linear)
//   sampler(0) = linear-repeat
//   buffer(1)  = MaterialConstants (roughness, metalness scalars)

#include "common.h"

// ─── Vertex ──────────────────────────────────────────────────────────────────────────────────

struct GBufVertIn
{
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
    float4 tangent  [[attribute(3)]];  // xyz = tangent, w = handedness (+1 or -1)
};

struct GBufVertOut
{
    float4 position      [[position]];
    float3 worldPos;          // interpolated world-space position (used by mirror projective UV)
    float3 worldNormal;
    float3 worldTangent;
    float3 worldBitangent;
    float2 uv;
    float4 currClip;     // jittered clip pos (for motion vectors)
    float4 prevClip;     // previous jittered clip pos
};

vertex GBufVertOut gbuffer_vert(
    GBufVertIn               in    [[stage_in]],
    constant FrameConstants& frame [[buffer(0)]],
    constant ModelConstants& model [[buffer(1)]])
{
    GBufVertOut out;

    float4 worldPos  = model.model * float4(in.position, 1.0);
    float4 clipPos   = frame.viewProj * worldPos;
    out.position     = clipPos;
    out.currClip     = clipPos;

    float4 prevWorld = model.prevModel * float4(in.position, 1.0);
    out.prevClip     = frame.prevViewProj * prevWorld;

    // Transform normal and tangent to world space via the normal matrix.
    float3 N = normalize((model.normalMat * float4(in.normal,      0.0)).xyz);
    float3 T = normalize((model.normalMat * float4(in.tangent.xyz, 0.0)).xyz);
    // Re-orthogonalise T against N (Gram-Schmidt).
    T = normalize(T - dot(T, N) * N);
    // Compute bitangent; handedness is stored in tangent.w.
    float3 B = cross(N, T) * in.tangent.w;

    out.worldPos       = worldPos.xyz;
    out.worldNormal    = N;
    out.worldTangent   = T;
    out.worldBitangent = B;
    out.uv             = in.uv;
    return out;
}

// ─── Fragment ─────────────────────────────────────────────────────────────────────────────────

struct GBufFragOut
{
    float4 albedoAO       [[color(0)]];  // RGBA8Unorm  — albedo.rgb + baked_AO.a
    float4 normalRoughMet [[color(1)]];  // RGBA8Unorm  — oct_normal.rg + roughness.b + metalness.a
    float4 emissive       [[color(2)]];  // RGBA16Float — emissive.rgb + outdoor flag (a)
    float2 motionVectors  [[color(3)]];  // RG16Float   — UV-space motion delta
};

fragment GBufFragOut gbuffer_frag(
    GBufVertOut                    in         [[stage_in]],
    texture2d<float>               albedoTex  [[texture(0)]],
    texture2d<float>               normalTex  [[texture(1)]],
    texture2d<float>               emissiveTex [[texture(2)]],
    sampler                        samp       [[sampler(0)]],
    constant FrameConstants&       frame      [[buffer(0)]],
    constant MaterialConstants&    mat        [[buffer(1)]])
{
    GBufFragOut out;

    // ─── Sprite sheet UV crop ───────────────────────────────────────────────────────────────────
    // For static geometry uvOffset=(0,0) and uvScale=(1,1) are identity (no crop).
    // For animated sprites the system writes the active frame cell before submission.
    float2 uv = in.uv * mat.uvScale + mat.uvOffset;

    // ─── Albedo ─────────────────────────────────────────────────────────────────────────────────
    float4 albedoSample = albedoTex.sample(samp, uv);

    // Alpha-cutout: discard pixels where the sprite/decal alpha mask is transparent.
    // Opaque geometry is unaffected (solid textures + the 1×1 white default have alpha=1).
    if (albedoSample.a < 0.5) { discard_fragment(); }

    // Apply per-draw tint.  Default tint = (1,1,1) so this is a no-op for normal
    // geometry.  The mirror surface sets tint = (0,0,0) to make its G-buffer albedo
    // black, which drives F0=0 in the lighting pass and eliminates the spurious
    // PBR specular + ambient contribution that was making the mirror look milky.
    float3 albedo = albedoSample.rgb * mat.tint.rgb;

    // RT0: albedo.rgb + baked AO stub (1.0 until SSAO is wired into the G-buffer)
    out.albedoAO = float4(albedo, 1.0);

    // ─── Tangent-space normal mapping ──────────────────────────────────────────────────────────
    // Sample [0,1] normal map and remap to [-1,+1] tangent-space vector.
    float3 tNormal = normalTex.sample(samp, uv).xyz * 2.0 - 1.0;
    // Build TBN and transform to world space.
    float3x3 TBN    = float3x3(in.worldTangent, in.worldBitangent, in.worldNormal);
    float3   wNormal = normalize(TBN * tNormal);

    // RT1: octahedral-encoded normal (RG) + roughness (B) + metalness (A)
    float2 octN = encode_normal(wNormal);
    // Remap [-1,+1] → [0,1] for RGBA8Unorm storage.
    out.normalRoughMet = float4(octN * 0.5 + 0.5, mat.roughness, mat.metalness);

    // ─── Emissive ────────────────────────────────────────────────────────────────────────────────────
    // For the mirror surface (isMirrorSurface == 1.0) the emissive slot holds the mirror RT.
    // Use the rasteriser-interpolated vertex world position (not a depth reconstruction) to
    // project through frame.mirrorViewProj.  Depth reconstruction amplifies tiny z-errors
    // enormously at grazing angles where the mirror VP is near-degenerate, causing UV drift
    // and edge-colour artefacts.  The vertex world position is exact.
    //
    // When the projected UV falls outside [0,1] the fragment lies outside the reflected
    // camera's frustum coverage — output black rather than clamping to the RT edge colour
    // (which is bright sky/scene content and produces a white flash).
    float3 emissiveRGB;
    if (mat.isMirrorSurface != 0.0)
    {
        float4 mirrorClip = frame.mirrorViewProj * float4(in.worldPos, 1.0);
        if (mirrorClip.w > 0.0)
        {
            float2 projUV = ndc_to_uv(mirrorClip.xy / mirrorClip.w);
            // Small epsilon prevents binary flicker at the exact frustum boundary.
            if (all(projUV >= -0.005f) && all(projUV <= 1.005f))
                emissiveRGB = emissiveTex.sample(samp, saturate(projUV)).rgb;
            else
                emissiveRGB = float3(0.0);  // outside reflected frustum → black
        }
        else
        {
            emissiveRGB = float3(0.0);  // behind reflected camera → black
        }
    }
    else
    {
        emissiveRGB = emissiveTex.sample(samp, uv).rgb;
    }
    // Bake per-sector ambient contribution (ambient color × intensity × albedo) into the
    // emissive channel.  The lighting pass reads this additively so each sector gets its
    // own ambient independent of the global frame.ambientColor (which is zeroed on the CPU).
    // Alpha channel carries the outdoor flag (1.0 = outdoor sector, 0.0 = indoor) so the
    // deferred lighting pass can gate directional sun contribution appropriately.
    out.emissive = float4(emissiveRGB + mat.sectorAmbient.xyz * albedo, mat.sectorAmbient.w);

    // ─── Motion vectors (NDC delta → UV delta) ──────────────────────────────────────
    float2 currNDC   = in.currClip.xy / in.currClip.w;
    float2 prevNDC   = in.prevClip.xy / in.prevClip.w;
    float2 jitterNDC = frame.jitter * frame.screenSize.zw * 2.0;
    float2 currUV    = ndc_to_uv(currNDC - jitterNDC);
    float2 prevUV    = ndc_to_uv(prevNDC);
    out.motionVectors = currUV - prevUV;

    return out;
}
