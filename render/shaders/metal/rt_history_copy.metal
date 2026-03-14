// rt_history_copy.metal
// Generic compute shader to copy one 2D texture to another.
//
// Used by the SVGF temporal pass to preserve the current frame's linear depth
// and encoded normal as "previous frame" data so that the next frame's
// disocclusion test can correctly detect camera motion / newly-revealed surfaces.
//
// Works with any texture format representable as float4:
//   • R32Float  (depth)
//   • RGBA8Unorm (normal)

#include "common.h"

kernel void rt_prev_copy_main(
    texture2d<float, access::read>  src [[texture(0)]],
    texture2d<float, access::write> dst [[texture(1)]],
    uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= src.get_width() || gid.y >= src.get_height()) return;
    dst.write(src.read(gid), gid);
}
