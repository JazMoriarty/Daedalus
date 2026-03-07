// dof_blur.metal
// Depth of Field — Pass 15.1: Poisson-disk bokeh blur.
//
// Performs a gather-based bokeh blur with a 12-tap Poisson disk kernel.
// The blur radius for each pixel is |CoC| pixels.  Output is split into
// two separate textures for correct near/far compositing:
//   dofFarTex  — pixels where CoC > 0  (behind focus plane)
//   dofNearTex — pixels where CoC < 0  (in front of focus plane)
//
// In-focus pixels (CoC ≈ 0) write the original scene colour to both outputs
// so the composite pass can blend without special-casing.
//
// Bindings
// --------
//   texture(0)  sceneColor  RGBA16F  source HDR frame (sample)
//   texture(1)  cocTex      R32F     signed CoC (read)
//   texture(2)  dofFarTex   RGBA16F  far-field blur result (write)
//   texture(3)  dofNearTex  RGBA16F  near-field blur result (write)
//   buffer(0)   FrameConstants
//   buffer(1)   DoFConstants
//   sampler(0)  linSamp     linear clamp

#include "common.h"

// 12-tap Poisson disk in the unit circle.
// Generated offline for well-distributed coverage with low discrepancy.
constant float2 k_poissonDisk[12] =
{
    float2(-0.326212f, -0.405810f),
    float2(-0.840144f, -0.073580f),
    float2(-0.695914f,  0.457137f),
    float2(-0.203345f,  0.620716f),
    float2( 0.962340f, -0.194983f),
    float2( 0.473434f, -0.480026f),
    float2( 0.519456f,  0.767022f),
    float2( 0.185461f, -0.893124f),
    float2( 0.507431f,  0.064425f),
    float2( 0.896420f,  0.412458f),
    float2(-0.321940f, -0.932615f),
    float2(-0.791559f, -0.597710f),
};

kernel void dof_blur(
    texture2d<float, access::sample>  sceneColor [[texture(0)]],
    texture2d<float, access::read>    cocTex     [[texture(1)]],
    texture2d<float, access::write>   dofFarTex  [[texture(2)]],
    texture2d<float, access::write>   dofNearTex [[texture(3)]],
    constant FrameConstants&          frame      [[buffer(0)]],
    constant DoFConstants&            dof        [[buffer(1)]],
    sampler                           linSamp    [[sampler(0)]],
    uint2                             tid        [[thread_position_in_grid]])
{
    const uint2  dims    = uint2(frame.screenSize.xy);
    if (tid.x >= dims.x || tid.y >= dims.y) return;

    const float2 invDims = frame.screenSize.zw;
    const float2 uv      = (float2(tid) + 0.5f) * invDims;

    const float coc        = cocTex.read(tid).r;
    const float blurRadius = abs(coc);

    // ── Poisson-disk gather ──────────────────────────────────────────────────
    float4 colFar  = float4(0.0f);
    float4 colNear = float4(0.0f);
    float  wFar    = 0.0f;
    float  wNear   = 0.0f;

    for (int i = 0; i < 12; ++i)
    {
        const float2 offset    = k_poissonDisk[i] * blurRadius * invDims;
        const float2 sampleUV  = uv + offset;
        const float4 s         = sceneColor.sample(linSamp, sampleUV);

        // Each sample's weight is 1 (equal contribution within the disk).
        colFar  += s;
        colNear += s;
        wFar    += 1.0f;
        wNear   += 1.0f;
    }

    // ── Centre sample (always contributes) ──────────────────────────────────
    const float4 centre = sceneColor.sample(linSamp, uv);
    colFar  += centre;  wFar  += 1.0f;
    colNear += centre;  wNear += 1.0f;

    // Normalise
    colFar  /= wFar;
    colNear /= wNear;

    // Write far-field output (coc > 0) and near-field output (coc < 0).
    // For pixels with no blur we still write the source colour — the
    // composite pass uses the CoC magnitude to determine blend weight.
    dofFarTex .write(coc > 0.0f ? colFar  : centre, tid);
    dofNearTex.write(coc < 0.0f ? colNear : centre, tid);
}
