// decal.metal
// Deferred decal pass — rasterises the decal OBB as a unit cube, samples the
// G-buffer depth to reconstruct the underlying surface position, tests it against
// the decal's local bounds, and writes a blended contribution back to the G-buffer.
//
// Render-state contract (set by FrameRenderer):
//   Depth test  : LessEqual       — only fragments in front of or at the surface
//   Depth write : false            — do NOT overwrite depth
//   Cull mode   : Back             — rasterise only the front-facing cube faces
//   RT0 blend   : srcAlpha / oneMinusSrcAlpha (RGB), zero / one (Alpha)
//   RT1 blend   : srcAlpha / oneMinusSrcAlpha (RGB), zero / one (Alpha)
//
// With those blend states the fragment's `.a` channel acts as the blend weight
// for the RGB channels, while the existing `.a` values in the G-buffer (AO for
// RT0, metalness for RT1) are fully preserved.
//
// Resource bindings:
//   buffer(0)  = FrameConstants  (vertex + fragment)
//   buffer(1)  = DecalConstants  (vertex + fragment)
//   buffer(2)  = fragCount       (device atomic_uint*, diagnostic counter, fragment only)
//   texture(0) = gDepthCopy      (R32Float copy of G-buffer depth, fragment only)
//   texture(1) = decal albedo    (RGBA8Unorm,   fragment only)
//   texture(2) = decal normal    (RGBA8Unorm,   fragment only — flat default when absent)
//   sampler(0) = linear-repeat   (fragment only)

#include "common.h"

// ─── Vertex ───────────────────────────────────────────────────────────────────
// We only need the position attribute from the unit-cube VBO; the stride still
// matches StaticMeshVertex (48 B) so the same buffer layout is reused.

struct DecalVertIn
{
    float3 position [[attribute(0)]];
    // Remaining attributes (normal, uv, tangent) exist in the VBO but are not
    // declared here — Metal simply ignores them when not referenced.
};

struct DecalVertOut
{
    float4 position [[position]];
};

vertex DecalVertOut decal_vert(
    DecalVertIn              in    [[stage_in]],
    constant FrameConstants& frame [[buffer(0)]],
    constant DecalConstants& decal [[buffer(1)]])
{
    DecalVertOut out;
    float4 worldPos = decal.model * float4(in.position, 1.0);
    out.position    = frame.viewProj * worldPos;
    return out;
}

// ─── Fragment ─────────────────────────────────────────────────────────────────

struct DecalFragOut
{
    float4 albedoAO       [[color(0)]];  // RT0 (RGBA8Unorm): albedo.rgb blended; .a = AO preserved by PSO
    float4 normalRoughMet [[color(1)]];  // RT1 (RGBA8Unorm): (octNorm, roughness) blended; .a = metalness preserved
};

fragment DecalFragOut decal_frag(
    DecalVertOut             in        [[stage_in]],
    texture2d<float>         gDepthCopy [[texture(0)]],  // R32Float copy — no feedback loop
    texture2d<float>         albedoTex [[texture(1)]],
    texture2d<float>         normalTex [[texture(2)]],
    sampler                  samp      [[sampler(0)]],
    constant FrameConstants& frame     [[buffer(0)]],
    constant DecalConstants& decal     [[buffer(1)]],
    device   atomic_uint*    fragCount [[buffer(2)]])   // diagnostic: counts live fragments
{
    // ─── Screen UV ───────────────────────────────────────────────────────────
    // in.position is window coords: x ∈ [0, width), y ∈ [0, height).
    // Multiply by reciprocal screen size to get [0,1] UV.
    float2 screenUV = in.position.xy * frame.screenSize.zw;

    // ─── G-buffer depth sample ───────────────────────────────────────────────
    // Use nearest-neighbour to avoid depth interpolation between samples.
    constexpr sampler depthSamp(filter::nearest,
                                mip_filter::none,
                                address::clamp_to_edge);
    float depth = gDepthCopy.sample(depthSamp, screenUV).r;

    // Sky pixels have depth ≈ 1.0 — no surface to project onto.
    if (depth >= 0.9999f) { discard_fragment(); }

    // ─── Reconstruct world-space surface position ─────────────────────────────
    // depth comes from gDepthCopy (R32Float), not the live depth attachment.
    float3 worldPos = reconstruct_world_pos(depth, screenUV, frame.invViewProj);

    // ─── Transform to decal local space ──────────────────────────────────────
    float4 localPos4 = decal.invModel * float4(worldPos, 1.0);
    float3 local     = localPos4.xyz / localPos4.w;

    // Discard pixels whose underlying surface lies outside the decal OBB.
    if (any(abs(local) > float3(0.5))) { discard_fragment(); }

    // ─── Decal UV ─────────────────────────────────────────────────────────────
    // Project local XZ plane → texture UV.
    //   U = local.x + 0.5   (−0.5..+0.5  →  0..1, left-to-right)
    //   V = 0.5 − local.z   (flip Z: +Z → V=0, −Z → V=1)
    float2 uv = float2(local.x + 0.5, 0.5 - local.z);

    // ─── Albedo ───────────────────────────────────────────────────────────────
    float4 albedoSample = albedoTex.sample(samp, uv);
    float  alpha        = albedoSample.a * decal.opacity;

    // Discard nearly-transparent fragments to avoid polluting the G-buffer.
    if (alpha < 0.01) { discard_fragment(); }

    // ─── Normal mapping ────────────────────────────────────────────────────────
    // Build TBN from the decal model matrix columns (world-space OBB axes).
    //   model[0].xyz  = local +X axis → tangent   (U increases along +X)
    //   model[1].xyz  = local +Y axis → surface normal (projection axis)
    //   model[2].xyz  = local +Z axis → V decreases along +Z → negate for bitangent
    float3 T = normalize(decal.model[0].xyz);
    float3 N = normalize(decal.model[1].xyz);
    float3 B = normalize(-decal.model[2].xyz);  // V = 0.5 − z  → flip

    float3   tNormal = normalTex.sample(samp, uv).xyz * 2.0 - 1.0;
    float3x3 TBN     = float3x3(T, B, N);
    float3   wNormal = normalize(TBN * tNormal);
    float2   octN    = encode_normal(wNormal);

    // ─── Output ────────────────────────────────────────────────────────────────
    // `alpha` is placed in the .a channel of both outputs.
    // The PSO configures:
    //   RGB  blend: srcFactor = SourceAlpha,  dstFactor = OneMinusSourceAlpha
    //   Alpha blend: srcFactor = Zero,         dstFactor = One
    // This means `alpha` drives the RGB lerp weight while the destination alpha
    // (AO for RT0, metalness for RT1) is left completely untouched.
    // Increment the diagnostic counter so the CPU can verify fragments are landing.
    atomic_fetch_add_explicit(fragCount, 1u, memory_order_relaxed);

    DecalFragOut out;
    out.albedoAO       = float4(albedoSample.rgb, alpha);
    out.normalRoughMet = float4(octN * 0.5 + 0.5, decal.roughness, alpha);
    return out;
}

// ─── RT composite variant ─────────────────────────────────────────────────────
// Used when the main render pass is path-traced (RT mode).
// The G-buffer is not available in RT mode, so decal albedo is blended directly
// into the HDR render target (premultiplied alpha, One / OneMinusSrcAlpha).
// Same OBB projection and position reconstruction as the raster variant.
// Lighting is not re-applied — acceptable approximation for editor RT preview.
//
// Resource bindings (reuses decal_vert vertex shader):
//   buffer(0)  = FrameConstants
//   buffer(1)  = DecalConstants
//   texture(0) = gDepthCopy  (R32Float, written by RT path)
//   texture(1) = decal albedo (RGBA8Unorm)
//   sampler(0) = linear-repeat

fragment float4 decal_rt_composite_frag(
    DecalVertOut             in         [[stage_in]],
    texture2d<float>         gDepthCopy [[texture(0)]],
    texture2d<float>         albedoTex  [[texture(1)]],
    sampler                  samp       [[sampler(0)]],
    constant FrameConstants& frame      [[buffer(0)]],
    constant DecalConstants& decal      [[buffer(1)]])
{
    float2 screenUV = in.position.xy * frame.screenSize.zw;

    constexpr sampler depthSamp(filter::nearest,
                                mip_filter::none,
                                address::clamp_to_edge);
    float depth = gDepthCopy.sample(depthSamp, screenUV).r;

    // RT path stores linear camera distance. Sky is written as far-plane depth.
    const float farLinearDepth = frame.proj[3][2] / (1.0 - frame.proj[2][2]);
    if (depth >= farLinearDepth * 0.999) { discard_fragment(); }

    // RT path stores linear camera distance in gDepthCopy; reconstruct world
    // position from camera ray + linear depth instead of treating depth as NDC.
    float2 ndc      = uv_to_ndc(screenUV);
    float4 farClip  = frame.invViewProj * float4(ndc, 1.0, 1.0);
    float3 farPos   = farClip.xyz / farClip.w;
    float3 camPos   = frame.cameraPos.xyz;
    float3 rayDir   = normalize(farPos - camPos);
    float3 worldPos = camPos + rayDir * depth;
    float4 localPos4 = decal.invModel * float4(worldPos, 1.0);
    float3 local     = localPos4.xyz / localPos4.w;

    // Discard fragments whose underlying surface lies outside the decal OBB.
    if (any(abs(local) > float3(0.5))) { discard_fragment(); }

    // Project local XZ → decal UV (same convention as raster variant).
    float2 uv = float2(local.x + 0.5, 0.5 - local.z);

    float4 albedoSample = albedoTex.sample(samp, uv);
    float  alpha        = albedoSample.a * decal.opacity;

    if (alpha < 0.01) { discard_fragment(); }

    // Premultiplied alpha output: src = One, dst = OneMinusSrcAlpha.
    return float4(albedoSample.rgb * alpha, alpha);
}
