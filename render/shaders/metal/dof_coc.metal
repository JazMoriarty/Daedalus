// dof_coc.metal
// Depth of Field — Pass 15: Circle of Confusion computation.
//
// Converts linearised scene depth to a signed Circle of Confusion (CoC) value
// in pixels for each screen pixel.  Positive CoC = far-field blur;
// negative CoC = near-field blur.  Zero means fully in focus.
//
// Algorithm
// ---------
//   linear depth  d  = ndc_to_linear_depth(ndcZ, proj)
//
//   farCoc  = saturate((d - focusDist - focusRange*0.5) / farTransition ) * bokehRadius
//   nearCoc = saturate((focusDist - focusRange*0.5 - d) / nearTransition) * bokehRadius
//   signedCoc = farCoc - nearCoc        (+bokehRadius = max far, -bokehRadius = max near)
//
// Bindings
// --------
//   texture(0)  gDepthCopy  R32F     raw NDC depth [0, 1]  (copy of hardware depth buffer)
//   texture(1)  cocOut      R32F     signed CoC output (write)
//   buffer(0)   FrameConstants
//   buffer(1)   DoFConstants

#include "common.h"

kernel void dof_coc(
    texture2d<float, access::read>   gDepthCopy [[texture(0)]],
    texture2d<float, access::write>  cocOut     [[texture(1)]],
    constant FrameConstants&         frame      [[buffer(0)]],
    constant DoFConstants&           dof        [[buffer(1)]],
    uint2                            tid        [[thread_position_in_grid]])
{
    const uint2 dims = uint2(frame.screenSize.xy);
    if (tid.x >= dims.x || tid.y >= dims.y) return;

    const float ndcZ = gDepthCopy.read(tid).r;

    // Sky / background: no blur.
    if (ndcZ >= 1.0f)
    {
        cocOut.write(float4(0.0f), tid);
        return;
    }

    // Linear view-space depth (positive, metres along view axis).
    const float d = ndc_to_linear_depth(ndcZ, frame.proj);

    // Far-field: pixels beyond the focus plane + half the focus band.
    const float farCoc  = saturate((d - dof.focusDistance - dof.focusRange * 0.5f)
                                   / max(dof.farTransition,  1e-4f)) * dof.bokehRadius;

    // Near-field: pixels closer than the focus plane – half the focus band.
    const float nearCoc = saturate((dof.focusDistance - dof.focusRange * 0.5f - d)
                                   / max(dof.nearTransition, 1e-4f)) * dof.bokehRadius;

    // Signed: positive = far blur, negative = near blur.
    const float signedCoc = farCoc - nearCoc;

    cocOut.write(float4(signedCoc, 0.0f, 0.0f, 0.0f), tid);
}
