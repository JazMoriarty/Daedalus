// depth_copy.metal
// Copies the G-buffer depth attachment (Depth32Float) to a standalone R32Float
// texture so the decal pass can sample depth without binding the same texture
// as both a render-pass attachment and a fragment shader resource in the same
// pass — which is undefined behaviour in Metal's automatic hazard-tracking model.
//
// Bindings:
//   texture(0) = gDepth      Depth32Float  (ShaderRead,  access::read)
//   texture(1) = gDepthCopy  R32Float      (ShaderWrite, access::write)

#include "common.h"

kernel void depth_copy_main(
    texture2d<float>                gDepth     [[texture(0)]],
    texture2d<float, access::write> gDepthCopy [[texture(1)]],
    uint2                           gid        [[thread_position_in_grid]])
{
    if (gid.x >= gDepthCopy.get_width() || gid.y >= gDepthCopy.get_height())
        return;
    // Copy the raw depth value [0, 1] into the red channel of the R32Float target.
    gDepthCopy.write(float4(gDepth.read(gid).r, 0.0, 0.0, 1.0), gid);
}
