// motion_blur_main.metal
// Motion Blur — Pass 16: velocity-cone weighted accumulation.
//
// Reads the G-buffer motion vector for each pixel and accumulates numSamples
// scene-colour samples along the blur direction with a triangular (cone)
// weight centred at the pixel.  The blur direction is velocity * shutterAngle.
//
// Algorithm
// ---------
//   blurDir = velocity.xy * shutterAngle          (UV-space displacement)
//   for t in linspace(-0.5, 0.5, numSamples):
//       sampleUV = uv + blurDir * t
//       weight   = max(0, 1 - |t| * 1.5)          (cone: 1 at centre, 0 at ±2/3)
//       accum   += sceneColor.sample(sampleUV) * weight
//   result = accum / totalWeight
//
// A pixel with zero velocity writes the source colour unchanged.
//
// Bindings
// --------
//   texture(0)  sceneColor  RGBA16F  post-DoF HDR frame (sample)
//   texture(1)  gMotionTex  RG16F    screen-space motion vectors (UV-space velocity)
//   texture(2)  mbOut       RGBA16F  motion-blurred output (write)
//   buffer(0)   FrameConstants
//   buffer(1)   MotionBlurConstants
//   sampler(0)  linSamp     linear clamp

#include "common.h"

kernel void motion_blur_main(
    texture2d<float, access::sample>  sceneColor  [[texture(0)]],
    texture2d<float, access::read>    gMotionTex  [[texture(1)]],
    texture2d<float, access::write>   mbOut       [[texture(2)]],
    constant FrameConstants&          frame       [[buffer(0)]],
    constant MotionBlurConstants&     mb          [[buffer(1)]],
    sampler                           linSamp     [[sampler(0)]],
    uint2                             tid         [[thread_position_in_grid]])
{
    const uint2 dims = uint2(frame.screenSize.xy);
    if (tid.x >= dims.x || tid.y >= dims.y) return;

    const float2 uv       = (float2(tid) + 0.5f) * frame.screenSize.zw;

    // UV-space velocity from G-buffer motion vector texture.
    const float2 velocity = gMotionTex.read(tid).rg;

    // Blur direction in UV space scaled by shutter open fraction.
    const float2 blurDir  = velocity * mb.shutterAngle;

    // Skip pixels with negligible motion.
    const float motionLen = length(blurDir);
    if (motionLen < 1e-5f)
    {
        mbOut.write(sceneColor.sample(linSamp, uv), tid);
        return;
    }

    // ── Cone-weighted accumulation ───────────────────────────────────────────

    const uint  n          = max(mb.numSamples, 1u);
    float4      accum      = float4(0.0f);
    float       totalWeight = 0.0f;

    for (uint i = 0u; i < n; ++i)
    {
        // t in [-0.5, 0.5]
        const float t      = (float(i) / float(n - 1u)) - 0.5f;
        const float weight = max(0.0f, 1.0f - abs(t) * 1.5f);

        const float2 sampleUV = uv + blurDir * t;
        accum       += sceneColor.sample(linSamp, sampleUV) * weight;
        totalWeight += weight;
    }

    mbOut.write(accum / max(totalWeight, 1e-6f), tid);
}
