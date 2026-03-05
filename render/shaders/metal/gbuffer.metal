// gbuffer.metal
// Geometry pass: fills the G-buffer with surface properties.
//
// G-buffer layout:
//   RT0 (RGBA8Unorm)  : albedo.rgb + roughness.a
//   RT1 (RGBA16Float) : world normal.xyz + metalness.a
//   RT2 (RG16Float)   : motion vectors (in UV-delta space)
//
// Depth attachment: Depth32Float (written by hardware).

#include "common.h"

// ─── Vertex ──────────────────────────────────────────────────────────────────

struct GBufVertIn
{
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
    float4 tangent  [[attribute(3)]];
};

struct GBufVertOut
{
    float4 position     [[position]];
    float3 worldNormal;
    float2 uv;
    float4 currClip;    // jittered clip pos (for motion)
    float4 prevClip;    // previous jittered clip pos
};

vertex GBufVertOut gbuffer_vert(
    GBufVertIn               in    [[stage_in]],
    constant FrameConstants& frame [[buffer(0)]],
    constant ModelConstants& model [[buffer(1)]])
{
    GBufVertOut out;

    float4 worldPos = model.model * float4(in.position, 1.0);
    float4 clipPos  = frame.viewProj * worldPos;

    // TAA jitter
    clipPos.xy += frame.jitter * clipPos.w;
    out.position = clipPos;

    // Previous-frame clip position (for motion vectors)
    float4 prevWorld  = model.prevModel * float4(in.position, 1.0);
    out.prevClip      = frame.prevViewProj * prevWorld;

    out.currClip    = clipPos;
    out.worldNormal = normalize((model.normalMat * float4(in.normal, 0.0)).xyz);
    out.uv          = in.uv;
    return out;
}

// ─── Fragment ─────────────────────────────────────────────────────────────────

struct GBufFragOut
{
    float4 albedoRoughness [[color(0)]];  // RGBA8Unorm
    float4 normalMetalness [[color(1)]];  // RGBA16Float
    float2 motionVectors   [[color(2)]];  // RG16Float
};

fragment GBufFragOut gbuffer_frag(
    GBufVertOut              in    [[stage_in]],
    constant FrameConstants& frame [[buffer(0)]])
{
    GBufFragOut out;

    // ─── Procedural checkerboard albedo ───────────────────────────────────────
    float2 scaled = in.uv * 4.0;
    float  check  = fmod(floor(scaled.x) + floor(scaled.y), 2.0);
    float3 albedo = mix(float3(0.85, 0.85, 0.85), float3(0.15, 0.15, 0.15), check);

    out.albedoRoughness = float4(albedo, 0.5);       // roughness = 0.5
    out.normalMetalness = float4(normalize(in.worldNormal), 0.0); // metalness = 0

    // ─── Motion vectors (NDC delta → UV delta) ────────────────────────────────
    float2 currNDC  = in.currClip.xy / in.currClip.w;
    float2 prevNDC  = in.prevClip.xy / in.prevClip.w;
    // Remove TAA jitter contribution from both frames
    float2 currUV   = ndc_to_uv(currNDC - frame.jitter);
    float2 prevUV   = ndc_to_uv(prevNDC - frame.jitter);
    out.motionVectors = currUV - prevUV;

    return out;
}
