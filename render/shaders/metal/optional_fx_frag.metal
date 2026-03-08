// optional_fx_frag.metal
// Optional FX — Pass 20: chromatic aberration, vignette, film grain.
//
// Three lightweight screen-space effects applied in a single fullscreen pass.
// Each effect can be disabled by setting the corresponding amount to 0.
//
// Vertex stage: reuses tonemap_vert (fullscreen triangle, TonemapVertOut).
//
// Bindings
// --------
//   texture(0)  src          BGRA8    LDR input (from CG, Tonemap, or prior pass)
//   sampler(0)  samp         linear clamp
//   buffer(0)   FrameConstants
//   buffer(1)   OptionalFxConstants
//
// Output: intermediate BGRA8Unorm (or swapchain when FXAA is disabled)

#include "common.h"

// Shared vertex output struct from tone_mapping.metal
struct TonemapVertOut
{
    float4 position [[position]];
    float2 uv;
};

// ─── PCG hash ─────────────────────────────────────────────────────────────────
// Produces a uniform uint from a seed uint; maps to [0,1) when divided by 2^32.
static inline uint pcg_hash(uint x)
{
    x = x * 747796405u + 2891336453u;
    x = ((x >> 8u) ^ x) * 277803737u;
    return (x >> 22u) ^ x;
}

// Returns a scalar noise in [-0.5, 0.5] for a given UV and frame seed.
// The UV is quantised to pixel coordinates using the screen dimensions so that
// each pixel gets an independent, temporally-varying grain sample.
static inline float grain_noise(float2 uv, float screenW, float screenH, float seed)
{
    const uint px = uint(uv.x * screenW);
    const uint py = uint(uv.y * screenH);
    // Mix pixel coords and frame seed into a single hash
    const uint h  = pcg_hash(px ^ pcg_hash(py ^ uint(seed * 1000.0f + 0.5f)));
    return float(h) / float(0xFFFFFFFFu) - 0.5f;   // [-0.5, 0.5]
}

fragment float4 optional_fx_frag(
    TonemapVertOut               in    [[stage_in]],
    texture2d<float>             src   [[texture(0)]],
    sampler                      samp  [[sampler(0)]],
    constant FrameConstants&     frame [[buffer(0)]],
    constant OptionalFxConstants& fx   [[buffer(1)]])
{
    // ── Chromatic aberration ──────────────────────────────────────────────────
    // Shift the R channel inward and the B channel outward along the radial
    // direction from the screen centre.  G stays at the original UV.
    float3 colour;
    if (fx.caAmount > 0.0f)
    {
        const float2 dir    = in.uv - 0.5f;           // vector from screen centre
        const float2 offset = dir * fx.caAmount;       // scale by aberration radius

        const float r = src.sample(samp, in.uv - offset).r;   // red shifted inward
        const float g = src.sample(samp, in.uv           ).g;  // green unshifted
        const float b = src.sample(samp, in.uv + offset  ).b;  // blue shifted outward
        colour = float3(r, g, b);
    }
    else
    {
        colour = src.sample(samp, in.uv).rgb;
    }

    // ── Vignette ─────────────────────────────────────────────────────────────
    // Squared distance from the centre UV (0.5,0.5).  Range is [0, 0.5] where
    // 0.5 is the corner.  vignetteRadius is the inner edge beyond which the
    // darkening begins; vignetteIntensity controls the maximum darkening.
    if (fx.vignetteIntensity > 0.0f)
    {
        const float2 centred = in.uv - 0.5f;
        const float  dist2   = dot(centred, centred);   // UV² from centre
        const float  fade    = smoothstep(fx.vignetteRadius, 0.5f, dist2);
        colour *= (1.0f - fade * fx.vignetteIntensity);
    }

    // ── Film grain ────────────────────────────────────────────────────────────
    // Adds a luma-weighted noise signal to simulate photographic film grain.
    // Noise amplitude is scaled by pixel luminance so bright areas have slightly
    // more grain and shadow areas are not overwhelmed by noise.
    if (fx.grainAmount > 0.0f)
    {
        const float  noise  = grain_noise(in.uv,
                                          frame.screenSize.x,
                                          frame.screenSize.y,
                                          fx.grainSeed);
        const float  luma   = luminance(colour);
        // Weight grain by a luma bell (peak around midtones; reduced in pure
        // black and pure white regions for a natural film-like appearance).
        const float  weight = 4.0f * luma * (1.0f - luma) + 0.2f;
        colour += noise * fx.grainAmount * weight;
    }

    return float4(saturate(colour), 1.0f);
}
