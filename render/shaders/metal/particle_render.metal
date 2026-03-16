// particle_render.metal
// Particle rendering: camera-facing procedural quads, atlas animation, soft particles.
//
// Pass 12 — Particles (after transparent, before TAA).
// Blends additively into the HDR render target.
// Depth tested (LessEqual) but not written; same depth buffer as transparent pass.
//
// Vertex bindings:
//   buffer(0) = FrameConstants              (view/proj/cameraDir)
//   buffer(1) = ParticleEmitterConstants    (atlas, velocityStretch, flip)
//   buffer(2) = ParticleGPU[]              stateBuffer  (read)
//   buffer(3) = uint[]                     aliveListA   (read)
//   buffer(4) = uint[]                     aliveListB   (read)
//
// Fragment bindings:
//   buffer(0) = FrameConstants              (proj for depth linearization, screenSize)
//   buffer(1) = ParticleEmitterConstants    (softRange, emissiveScale)
//   texture(0) = atlas (RGBA8Unorm)
//   texture(1) = gDepthCopy (R32Float, for soft-particle fade)
//   sampler(0) = linear clamp (atlas + depth)
//
// Blending:
//   Premultiplied alpha: src = One, dst = OneMinusSrcAlpha.
//   Fragment outputs premultiplied color: out.rgb = color.rgb * alpha * emissiveScale,
//                                         out.a   = alpha.
//   This unifies purely additive (alpha ≈ 0 → additive) and regular alpha (alpha > 0).

#include "common.h"

// ─── Quad geometry tables ─────────────────────────────────────────────────────
// Two CCW triangles per quad (vertex_id 0–5).

constant float2 k_corners[4] = {
    { -0.5,  -0.5 },   // 0 bottom-left
    {  0.5,  -0.5 },   // 1 bottom-right
    {  0.5,   0.5 },   // 2 top-right
    { -0.5,   0.5 },   // 3 top-left
};
constant uint k_quadIdx[6] = { 0, 1, 2,   2, 3, 0 };

// UV (0,0) = top-left in Metal texture convention.
constant float2 k_uvs[4] = {
    { 0.0, 1.0 },  // bottom-left  (u=0, v=1)
    { 1.0, 1.0 },  // bottom-right (u=1, v=1)
    { 1.0, 0.0 },  // top-right    (u=1, v=0)
    { 0.0, 0.0 },  // top-left     (u=0, v=0)
};

// ─── Vertex / fragment I/O ────────────────────────────────────────────────────

struct PartVertOut
{
    float4 clipPos  [[position]];
    float2 uv;
    float4 color;             // HDR tint (rgb) + opacity (a), already life-interpolated by simulate
    float  emissive;          // emissiveScale from constants (passed to fragment)
    float  softRange;         // em.softRange
    float  particleZ;         // view-space depth (raster mode depth compare)
    float  particleLinearDist; // radial camera distance (RT mode depth compare)
};

// ─── Depth linearization helper ───────────────────────────────────────────────
// Converts Metal [0,1] NDC depth to view-space Z using the ZO projection matrix.
// For glm::perspectiveLH_ZO: viewZ = proj[3][2] / (ndcDepth - proj[2][2])

static inline float ndcToViewZ(float d, float4x4 proj)
{
    return proj[3][2] / (d - proj[2][2]);
}

// ─── Vertex shader ────────────────────────────────────────────────────────────

vertex PartVertOut particle_vert(
    uint                               vid         [[vertex_id]],
    uint                               iid         [[instance_id]],
    constant FrameConstants&           frame       [[buffer(0)]],
    constant ParticleEmitterConstants& em          [[buffer(1)]],
    device const ParticleGPU*          state       [[buffer(2)]],
    device const uint*                 aliveA      [[buffer(3)]],
    device const uint*                 aliveB      [[buffer(4)]])
{
    // Resolve alive list: compact writes to the write list, so we read from it.
    // flip==0: simulate read A, wrote B  → render reads B
    // flip==1: simulate read B, wrote A  → render reads A
    device const uint* aliveList = (em.aliveListFlip == 0u) ? aliveB : aliveA;

    // Map instance → particle index
    uint particleIdx = aliveList[1u + iid];
    ParticleGPU p    = state[particleIdx];

    // ── Camera-space basis vectors ────────────────────────────────────────────
    // The view matrix rows (upper-left 3×3) give the camera's world-space axes.
    // In GLM/MSL column-major: view[col][row], so row i = {view[0][i], view[1][i], view[2][i]}.
    float3 camRight = normalize(float3(frame.view[0][0], frame.view[1][0], frame.view[2][0]));
    float3 camUp    = normalize(float3(frame.view[0][1], frame.view[1][1], frame.view[2][1]));
    float3 camFwd   = frame.cameraDir.xyz;  // normalised forward in world space

    // ── Velocity stretching ───────────────────────────────────────────────────
    // Project velocity onto the camera plane and stretch the quad along that axis.
    float3 worldRight = camRight;
    float3 worldUp    = camUp;

    if (em.velocityStretch > 0.0 && length_squared(p.vel) > 1e-6)
    {
        // Velocity projected onto the camera plane (remove component along camera forward).
        float3 velPlane = p.vel - dot(p.vel, camFwd) * camFwd;
        float  velLen   = length(velPlane);
        if (velLen > 0.001)
        {
            float  stretchFactor = 1.0 + velLen * em.velocityStretch;
            float3 stretchDir    = velPlane / velLen;
            float3 perpDir       = cross(camFwd, stretchDir);   // perpendicular on camera plane

            worldRight = perpDir;                               // width axis (not stretched)
            worldUp    = stretchDir * stretchFactor;            // height axis (stretched)
        }
    }

    // ── Quad corner in world space ────────────────────────────────────────────
    uint   qi     = k_quadIdx[vid];
    float2 corner = k_corners[qi];

    // Apply billboard rotation (sin/cos applied to the 2D local corner).
    float s = sin(p.rot);
    float c = cos(p.rot);
    float2 rotCorner = float2(corner.x * c - corner.y * s,
                              corner.x * s + corner.y * c);

    float3 worldPos = p.pos
                    + worldRight * (rotCorner.x * p.size)
                    + worldUp    * (rotCorner.y * p.size);

    float4 clipPos = frame.viewProj * float4(worldPos, 1.0);

    // ── Atlas UV ──────────────────────────────────────────────────────────────
    uint   cols  = max(1u, uint(em.atlasSize.x));
    uint   rows  = max(1u, uint(em.atlasSize.y));
    uint   col   = p.frameIdx % cols;
    uint   row   = p.frameIdx / cols;
    float2 cellUV = k_uvs[qi];                                // [0,1] within cell
    float2 atlaUV = float2((float(col) + cellUV.x) / float(cols),
                           (float(row) + cellUV.y) / float(rows));

    // ── Pass-through depth values for soft particle compare ──────────────────
    // particleZ       : view-space depth — used in raster mode.
    // particleLinearDist: radial camera distance — used in RT mode.
    float particleViewZ = ndcToViewZ(clipPos.z / clipPos.w, frame.proj);

    PartVertOut out;
    out.clipPos          = clipPos;
    out.uv               = atlaUV;
    out.color            = p.color;
    out.emissive         = p.emissive * em.emissiveScale;  // per-particle emissive × global multiplier
    out.softRange        = em.softRange;
    out.particleZ        = particleViewZ;
    out.particleLinearDist = length(worldPos - float3(frame.cameraPos.xyz));
    return out;
}

// ─── Fragment shader ──────────────────────────────────────────────────────────

fragment float4 particle_frag(
    PartVertOut                        in          [[stage_in]],
    constant FrameConstants&           frame       [[buffer(0)]],
    constant ParticleEmitterConstants& em          [[buffer(1)]],
    texture2d<float>                   atlasTex    [[texture(0)]],
    texture2d<float>                   depthCopy   [[texture(1)]],
    sampler                            clampSamp   [[sampler(0)]])
{
    // ── Atlas sample ──────────────────────────────────────────────────────────
    float4 texSample = atlasTex.sample(clampSamp, in.uv);
    float3 albedo    = texSample.rgb;
    float  texAlpha  = texSample.a;

    // ── Depth test & soft particle fade ──────────────────────────────────────
    // The depth copy texture carries different values depending on render mode:
    //   Raster: NDC depth [0,1] from the G-buffer depth-copy pass.
    //   RT    : linear world-space camera distance (metres) from path_trace.metal.
    // We branch on frame.isRTMode and work in the appropriate space so that
    // both the hard occlusion discard and the soft fade use consistent units.
    float2 screenUV     = in.clipPos.xy / frame.screenSize.xy;
    float  opaqueDepth;    // opaque surface depth in the comparison space
    float  particleDepth;  // this particle's depth in the same space

    if (frame.isRTMode > 0.5f)
    {
        // RT mode: depth texture stores linear (radial) camera distance.
        // Compare directly — both values are in world-space metres.
        opaqueDepth   = depthCopy.sample(clampSamp, screenUV).r;
        particleDepth = in.particleLinearDist;
    }
    else
    {
        // Raster mode: depth texture stores NDC depth → convert to view-space Z.
        float opaqueNDC = depthCopy.sample(clampSamp, screenUV).r;
        opaqueDepth     = ndcToViewZ(opaqueNDC, frame.proj);
        particleDepth   = in.particleZ;
    }

    // Hard occlusion: discard particles that are behind opaque geometry.
    // The 0.01 m bias avoids z-fighting at contact points.
    if (particleDepth > opaqueDepth + 0.01f) { discard_fragment(); }

    // Soft fade: gradually fade particles close to an opaque surface
    // to avoid a hard geometric intersection seam.
    float softFade = 1.0f;
    if (em.softRange > 0.0f)
    {
        float zDiff = abs(opaqueDepth - particleDepth);
        softFade    = saturate(zDiff / em.softRange);
    }

    // ── Alpha ─────────────────────────────────────────────────────────────────
    float alpha = in.color.a * texAlpha * softFade;

    // ── Scene lighting ────────────────────────────────────────────────────────
    // Camera-facing billboard: normal always points toward the viewer.
    float3 N        = -frame.cameraDir.xyz;
    float  sunNdotL = max(dot(N, frame.sunDirection.xyz), 0.0f);
    float3 sunLight = frame.sunColor.xyz * frame.sunColor.w * sunNdotL;
    float3 ambient  = frame.ambientColor.xyz;
    float3 sceneLight = sunLight + ambient;

    // ── Emissive blend ────────────────────────────────────────────────────────
    // in.emissive = p.emissive * em.emissiveScale (per-particle × global multiplier).
    // At emissive=1: fully self-lit (classic additive look).
    // At emissive=0: fully scene-lit (particle receives sun + ambient as albedo).
    float  litFactor = saturate(1.0f - in.emissive);
    float3 hdrColor  = albedo * in.color.rgb * (in.emissive + litFactor * sceneLight) * alpha;

    // Premultiplied alpha output: dst blend = One / OneMinusSrcAlpha.
    return float4(hdrColor, alpha);
}
