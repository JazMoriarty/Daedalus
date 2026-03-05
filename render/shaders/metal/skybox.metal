// skybox.metal
// Procedural sky rendered as a full-screen pass.
// Only writes to pixels where depth == 1 (background / no geometry).
//
// Render pass uses LoadAction::Load on the HDR colour target so that
// geometry pixels are preserved.

#include "common.h"

// ─── Fullscreen triangle ──────────────────────────────────────────────────────

struct SkyVertOut
{
    float4 position [[position]];
    float2 uv;
};

// Draws one large triangle that covers the viewport.
vertex SkyVertOut skybox_vert(uint vid [[vertex_id]])
{
    // Vertices of a triangle that completely covers clip space
    const float2 positions[3] = {
        float2(-1.0, -3.0),
        float2(-1.0,  1.0),
        float2( 3.0,  1.0)
    };
    SkyVertOut out;
    out.position = float4(positions[vid], 1.0, 1.0); // depth = 1 → far plane
    out.uv       = (positions[vid] + 1.0) * 0.5;
    return out;
}

// ─── Sky colour ───────────────────────────────────────────────────────────────

fragment float4 skybox_frag(
    SkyVertOut               in    [[stage_in]],
    constant FrameConstants& frame [[buffer(0)]])
{
    // Reconstruct view ray
    float2 ndc    = uv_to_ndc(in.uv);
    float4 viewRay = frame.invViewProj * float4(ndc, 0.0, 1.0);
    float3 dir    = normalize(viewRay.xyz / viewRay.w - frame.cameraPos.xyz);

    float  t      = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0); // horizon blend

    // Horizon and zenith colours
    float3 horizon = float3(0.70, 0.82, 0.95);
    float3 zenith  = float3(0.25, 0.48, 0.80);
    float3 sky     = mix(horizon, zenith, pow(t, 1.5));

    // Simple sun disc
    float3 sunDir  = normalize(frame.sunDirection.xyz);
    float  sunDot  = dot(dir, sunDir);
    float  sunDisc = smoothstep(0.998, 1.0, sunDot);
    float3 sunCol  = frame.sunColor.xyz * frame.sunColor.w;
    sky += sunCol * sunDisc * 5.0;

    return float4(sky, 1.0);
}
