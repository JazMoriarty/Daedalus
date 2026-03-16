// path_trace.metal
// GPU path tracing compute kernel for the RT render mode.
// One thread per pixel — writes HDR colour + auxiliary guide signals
// (normal, depth, motion vectors) for the SVGF denoiser.
//
// Phase 2: per-pixel texture sampling via barycentric interpolation.
// Albedo, normal map, and emissive textures are sampled from a texture
// array bound at texture(4+).  Per-triangle vertex attributes (UV,
// normal, tangent) are read from a flat primitive data buffer.

#include "common.h"
#include <metal_raytracing>

using namespace metal::raytracing;

// Metal compute shaders support at most 128 texture slots.  Slots 0-3 are
// reserved for write-only output textures, leaving 124 for the material
// texture table.  96 provides comfortable headroom.
constant uint MAX_RT_TEXTURES = 96;

// ─── PCG random number generator ─────────────────────────────────────────────
// Deterministic, no texture lookups.  Seeded from (pixel, frame).

struct RNG
{
    uint state;

    RNG(uint2 pixel, uint frame)
    {
        state = pixel.x * 1973u + pixel.y * 9277u + frame * 26699u;
        state = pcg(state);
    }

    uint pcg(uint v)
    {
        v = v * 747796405u + 2891336453u;
        uint word = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
        return (word >> 22u) ^ word;
    }

    /// Uniform float in [0, 1).
    float next()
    {
        state = pcg(state);
        return float(state) / float(0xFFFFFFFFu);
    }

    /// Uniform float2 in [0, 1)^2.
    float2 next2()
    {
        return float2(next(), next());
    }
};

// ─── Sampling helpers ─────────────────────────────────────────────────────────

/// Cosine-weighted hemisphere sample oriented along +Z.
inline float3 cosine_hemisphere(float2 u)
{
    float r   = sqrt(u.x);
    float phi = 2.0f * PI * u.y;
    return float3(r * cos(phi), r * sin(phi), sqrt(max(0.0f, 1.0f - u.x)));
}

/// Build an orthonormal basis from a normal vector.
inline void make_basis(float3 N, thread float3& T, thread float3& B)
{
    float3 up = abs(N.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
    T = normalize(cross(up, N));
    B = cross(N, T);
}

/// Transform a local-space direction to world-space using an ONB.
inline float3 to_world(float3 local, float3 T, float3 B, float3 N)
{
    return local.x * T + local.y * B + local.z * N;
}

// ─── Procedural sky ──────────────────────────────────────────────────────────
// Simple gradient matching the raster skybox aesthetic.

inline float3 sky_color(float3 dir, float3 sunDir, float3 sunCol, float sunIntensity)
{
    // Base gradient: blue-white
    float t = 0.5f * (dir.y + 1.0f);
    float3 sky = mix(float3(1.0f), float3(0.5f, 0.7f, 1.0f), t);

    // Sun disc
    float sunDot = max(dot(dir, sunDir), 0.0f);
    sky += sunCol * sunIntensity * pow(sunDot, 256.0f);

    return sky;
}

// ─── Surface evaluation from intersection ────────────────────────────────────
// Shared by primary hit and indirect bounces.  Reads primitive data and
// material table to produce shading-ready surface attributes.

struct SurfaceHit
{
    float3 position;
    float3 normal;    // world-space shading normal (geometry or normal-mapped)
    float3 geoNormal; // world-space geometric normal (for bias)
    float3 albedo;
    float3 emissive;
    float  roughness;
    float  metalness;
    float4 sectorAmbient;
};

inline SurfaceHit evaluate_surface(
    float3 rayOrigin,
    float3 rayDir,
    float  hitDist,
    uint   instId,
    uint   primId,
    float2 barycentrics,
    device const RTMaterialGPU*    materials,
    device const RTPrimitiveData*  primitives,
    array<texture2d<float>, MAX_RT_TEXTURES> textures,
    sampler texSampler,
    float  pixelAngle)  // ray-cone half-angle per pixel; drives explicit mip LOD
{
    SurfaceHit surf;
    surf.position = rayOrigin + rayDir * hitDist;

    const device RTMaterialGPU& mat = materials[instId];

    // Barycentric weights: w0 = 1 - bary.x - bary.y,  w1 = bary.x,  w2 = bary.y
    const float w1 = barycentrics.x;
    const float w2 = barycentrics.y;
    const float w0 = 1.0f - w1 - w2;

    // Read per-triangle vertex attributes.
    const device RTPrimitiveData& tri = primitives[mat.primitiveDataOffset + primId];

    // Interpolate UV.
    float2 uv = float2(tri.uv0) * w0 + float2(tri.uv1) * w1 + float2(tri.uv2) * w2;
    uv = uv * mat.uvScale + mat.uvOffset;

    // Interpolate geometric normal (object-space, normalise after transform).
    float3 interpN = float3(tri.normal0) * w0 + float3(tri.normal1) * w1
                   + float3(tri.normal2) * w2;
    interpN = normalize(interpN);

    // Ensure the geometric normal faces the incoming ray.
    if (dot(interpN, -rayDir) < 0.0f)
        interpN = -interpN;
    surf.geoNormal = interpN;

    // ── Explicit mip LOD from ray cone ───────────────────────────────────────
    // Metal compute kernels have no screen-space derivatives, so .sample() with
    // a mipFilter sampler still defaults to LOD 0 — identical to nearest-mip.
    // This causes severe texture aliasing (moiré, swimming) because every hit,
    // regardless of surface distance or viewing angle, samples the highest-
    // resolution mip.
    //
    // Fix: model the camera as a ray cone whose half-angle is pixelAngle
    // (one pixel's angular extent at the vertical screen centre).  At hit
    // distance d the cone's footprint on the surface has a world-space
    // radius of d × pixelAngle / NdotV (glancing-angle correction).
    // Converting to texels and taking log2 gives the correct mip level.
    //
    // pixelAngle = 2 × tan(fovY/2) / screenHeight
    //            = 2 / (proj[1][1] × screenHeight)   (proj[1][1] = 1/tan(fovY/2))
    //
    // This is computed once per path_trace_main invocation and passed here.
    float texLOD = 0.0f;
    {
        float NdotV = max(abs(dot(interpN, -rayDir)), 0.1f);
        // Use the albedo texture dimensions as the reference; normal/emissive
        // textures for the same material are typically the same resolution.
        float texW = float(textures[mat.albedoTextureIndex].get_width());
        float texH = float(textures[mat.albedoTextureIndex].get_height());
        float footprint = hitDist * pixelAngle
                        * max(texW * mat.uvScale.x, texH * mat.uvScale.y)
                        / NdotV;
        texLOD = max(0.0f, log2(footprint));
    }

    // ── Sample textures ──────────────────────────────────────────────────────

    // Albedo: texture × tint.
    float4 albedoSample = textures[mat.albedoTextureIndex].sample(texSampler, uv, level(texLOD));
    surf.albedo = albedoSample.rgb * mat.tint.rgb;

    // Normal mapping: decode tangent-space normal, build TBN, transform.
    float3 shadingN = interpN;
    if (mat.normalTextureIndex != 0)
    {
        float3 tangentNormal = textures[mat.normalTextureIndex].sample(texSampler, uv, level(texLOD)).xyz;
        tangentNormal = tangentNormal * 2.0f - 1.0f;

        // Interpolate tangent.
        float4 interpT = float4(tri.tangent0) * w0 + float4(tri.tangent1) * w1
                       + float4(tri.tangent2) * w2;
        float3 T = normalize(float3(interpT.xyz));
        float  handedness = interpT.w;
        float3 B = cross(interpN, T) * handedness;

        shadingN = normalize(T * tangentNormal.x + B * tangentNormal.y
                           + interpN * tangentNormal.z);
    }
    surf.normal = shadingN;

    // Emissive.
    if (mat.emissiveTextureIndex != 0)
        surf.emissive = textures[mat.emissiveTextureIndex].sample(texSampler, uv, level(texLOD)).rgb;
    else
        surf.emissive = float3(0.0f);

    surf.roughness     = mat.roughness;
    surf.metalness     = mat.metalness;
    surf.sectorAmbient = mat.sectorAmbient;

    return surf;
}

// ─── Particle shadow transmittance ───────────────────────────────────────────

/// Ray-AABB intersection.  Returns (tEnter, tExit).  No hit when tEnter > tExit.
inline float2 aabb_intersect(float3 ro, float3 rd, float3 boxMin, float3 boxMax)
{
    float3 invDir = 1.0f / rd;
    float3 t0 = (boxMin - ro) * invDir;
    float3 t1 = (boxMax - ro) * invDir;
    float3 tMin = min(t0, t1);
    float3 tMax = max(t0, t1);
    float tEnter = max(max(tMin.x, tMin.y), tMin.z);
    float tExit  = min(min(tMax.x, tMax.y), tMax.z);
    return float2(tEnter, tExit);
}

/// Beer-Lambert transmittance of a ray through all active density shadow volumes.
/// Returns T in [0, 1].  Short-circuits immediately when no volumes exist (zero overhead).
inline float compute_particle_transmittance(
    float3 rayOrigin,
    float3 rayDir,
    float  rayMaxDist,
    uint   shadowVolumeCount,
    device const ParticleShadowVolumeGPU* shadowVolumes,
    array<texture3d<float>, MAX_PARTICLE_SHADOW_EMITTERS> densityTextures,
    sampler densitySampler)
{
    if (shadowVolumeCount == 0u) return 1.0f;

    float T = 1.0f;
    const int kMarchSteps = 16;

    for (uint i = 0; i < shadowVolumeCount; ++i)
    {
        float2 hit = aabb_intersect(rayOrigin, rayDir,
                                    float3(shadowVolumes[i].worldMin),
                                    float3(shadowVolumes[i].worldMax));

        // Miss: no intersection, behind ray origin, or beyond ray max distance.
        if (hit.x > hit.y || hit.y < 0.0f || hit.x > rayMaxDist) continue;

        float tStart = max(hit.x, 0.0f);
        float tEnd   = min(hit.y, rayMaxDist);
        float stride = (tEnd - tStart) / float(kMarchSteps);

        float3 volSize = float3(shadowVolumes[i].worldMax) - float3(shadowVolumes[i].worldMin);
        float  opticalDepth = 0.0f;

        for (int s = 0; s < kMarchSteps; ++s)
        {
            float  t   = tStart + (float(s) + 0.5f) * stride;
            float3 pos = rayOrigin + rayDir * t;
            float3 uvw = clamp((pos - float3(shadowVolumes[i].worldMin)) / volSize,
                               float3(0.001f), float3(0.999f));
            float  dens = densityTextures[i].sample(densitySampler, uvw).r;
            opticalDepth += dens * stride;
        }

        T *= exp(-shadowVolumes[i].shadowDensity * opticalDepth);
    }
    return T;
}

/// Volumetric emission from particle density volumes onto a surface point.
/// For each active emissive volume, shoots one hard-occlusion shadow ray toward
/// the closest point on the volume AABB (not the centroid) so surfaces near the
/// edge of a cloud receive emission even when the centroid is outside their
/// hemisphere.  The density integral still marches toward the centroid so the
/// full cloud depth is sampled consistently.
/// Returns the total incident emissive radiance (L_e × NdotL) contributed to
/// the surface, ready to be multiplied by albedo at the call site.
/// Called at every primary hit and at each indirect bounce hit so particles
/// illuminate floor, ceiling, walls, and other objects in all directions.
inline float3 compute_particle_emission(
    float3 surfPos,
    float3 surfNormal,
    uint   shadowVolumeCount,
    device const ParticleShadowVolumeGPU*                   shadowVolumes,
    array<texture3d<float>, MAX_PARTICLE_SHADOW_EMITTERS>   densityTextures,
    sampler                                                  densitySampler,
    instance_acceleration_structure                          tlas)
{
    if (shadowVolumeCount == 0u) return float3(0.0f);

    float3 Le = float3(0.0f);
    const int kMarchSteps = 8;

    for (uint i = 0; i < shadowVolumeCount; ++i)
    {
        if (shadowVolumes[i].emissiveIntensity <= 0.0f) continue;

        float3 volMin   = float3(shadowVolumes[i].worldMin);
        float3 volMax   = float3(shadowVolumes[i].worldMax);
        float3 centroid = 0.5f * (volMin + volMax);

        // centroid direction: used for the density march so we always sample
        // through the full cloud depth regardless of approach angle.
        float3 toCentroid = centroid - surfPos;
        float  dist       = length(toCentroid);
        if (dist < 0.001f) continue;
        float3 centerDir  = toCentroid / dist;

        // Closest-point direction: used for NdotL and the shadow ray.
        // A surface adjacent to the volume whose normal faces away from the
        // centroid can still have the nearest AABB face in its front hemisphere,
        // removing the strong directional bias of the centroid-only approach.
        float3 closestPt   = clamp(surfPos, volMin, volMax);
        float3 toClosest   = closestPt - surfPos;
        float  closestDist = length(toClosest);

        // If we are inside the volume (closestDist ≈ 0) fall back to centroid.
        float3 sampleDir     = (closestDist > 0.001f) ? (toClosest / closestDist) : centerDir;
        float  shadowMaxDist = (closestDist > 0.001f) ? closestDist : dist;

        float NdotL = max(dot(surfNormal, sampleDir), 0.0f);
        if (NdotL <= 0.0f) continue;

        // Hard geometry occlusion along the closest-point direction.
        ray shadowRay;
        shadowRay.origin       = surfPos + surfNormal * 0.001f;
        shadowRay.direction    = sampleDir;
        shadowRay.min_distance = 0.001f;
        shadowRay.max_distance = shadowMaxDist;

        intersector<instancing> emIsect;
        emIsect.accept_any_intersection(true);
        if (emIsect.intersect(shadowRay, tlas).type != intersection_type::none) continue;

        // Density integral marches toward the centroid for consistent sampling.
        float2 hit = aabb_intersect(surfPos, centerDir, volMin, volMax);
        if (hit.x > hit.y || hit.y < 0.0f || hit.x > dist) continue;

        float  tStart = max(hit.x, 0.0f);
        float  tEnd   = min(hit.y, dist);
        float  stride = (tEnd - tStart) / float(kMarchSteps);
        float3 volSize = volMax - volMin;

        float integral = 0.0f;
        for (int s = 0; s < kMarchSteps; ++s)
        {
            float  t   = tStart + (float(s) + 0.5f) * stride;
            float3 pos = surfPos + centerDir * t;
            float3 uvw = clamp((pos - volMin) / volSize, float3(0.001f), float3(0.999f));
            integral  += densityTextures[i].sample(densitySampler, uvw).r * stride;
        }

        // Accumulate: emissive radiance × density integral × falloff × NdotL.
        // dist is always the centroid distance so falloff is stable regardless
        // of whether we approached from the near or far side of the volume.
        const float kScale = 5.0f;
        float atten = 1.0f / (dist + 1.0f);
        Le += float3(shadowVolumes[i].emissiveColor)
            * shadowVolumes[i].emissiveIntensity
            * integral * atten * NdotL * kScale;
    }
    return Le;
}

// ─── path_trace_main ───────────────────────────────────────────────

kernel void path_trace_main(
    constant FrameConstants&    frame       [[buffer(0)]],
    constant RTConstants&       rtConst     [[buffer(1)]],
    device const RTMaterialGPU* materials   [[buffer(2)]],
    device const PointLightGPU* lights      [[buffer(3)]],
    device const uint&          lightCount  [[buffer(4)]],
    instance_acceleration_structure tlas     [[buffer(5)]],
    device const RTPrimitiveData*  primitives [[buffer(6)]],
    device const SpotLightGPU*  spotLights    [[buffer(7)]],
    device const uint&          spotLightCount [[buffer(8)]],
    device const uint&                        shadowVolumeCount [[buffer(9)]],
    device const ParticleShadowVolumeGPU*     shadowVolumes     [[buffer(10)]],
    texture2d<float, access::write> outHDR    [[texture(0)]],
    texture2d<float, access::write> outNormal [[texture(1)]],
    texture2d<float, access::write> outDepth  [[texture(2)]],
    texture2d<float, access::write> outMotion [[texture(3)]],
    texture2d<float, access::write> outAlbedo [[texture(4)]],
    array<texture2d<float>, MAX_RT_TEXTURES> textures [[texture(5)]],
    array<texture3d<float>, MAX_PARTICLE_SHADOW_EMITTERS> densityTextures [[texture(101)]],
    sampler texSampler [[sampler(0)]],
    uint2 gid [[thread_position_in_grid]])
{
    const uint2 screenSize = uint2(frame.screenSize.xy);
    if (gid.x >= screenSize.x || gid.y >= screenSize.y) return;

    // ─── Pixel-cone half-angle ────────────────────────────────────────────────
    // frame.proj[1][1] = 1/tan(fovY/2) for a column-major perspective matrix.
    // pixelAngle is the angle subtended by one pixel at the screen centre.
    // Passed to evaluate_surface so textures are sampled at the correct mip
    // level for each hit distance rather than always defaulting to LOD 0.
    const float pixelAngle = 2.0f / (frame.proj[1][1] * float(screenSize.y));

    // ─── G-buffer primary ray (pixel centre, deterministic) ──────────────────
    // The denoiser guide signals (depth, normal, motion vectors) must be stable
    // frame-to-frame so SVGF reprojection sees consistent geometry.  They are
    // always computed from the exact pixel-centre direction, never from a
    // jittered sample.
    //
    // The per-sample jitter below varies the radiance computation each frame,
    // giving temporal AA via SVGF accumulation at SPP=1 and additional spatial
    // AA within the frame at SPP>1.  This is the correct implementation of the
    // design-spec requirement (§ Path tracing kernel, step 1) without the SVGF
    // instability caused by the earlier Halton approach, which jittered the
    // G-buffer ray and produced spurious motion vectors on every static surface.

    float2 pixel = float2(gid) + 0.5f;
    float2 uv    = pixel / float2(screenSize);
    float2 ndc   = uv_to_ndc(uv);

    float4 nearClip = frame.invViewProj * float4(ndc, 0.0f, 1.0f);
    float4 farClip  = frame.invViewProj * float4(ndc, 1.0f, 1.0f);
    float3 origin   = nearClip.xyz / nearClip.w;  // camera position (perspective)
    float3 target   = farClip.xyz / farClip.w;
    float3 centerDir = normalize(target - origin);

    ray centerRay;
    centerRay.origin       = origin;
    centerRay.direction    = centerDir;
    centerRay.min_distance = 0.001f;
    centerRay.max_distance = 1e20f;

    intersector<triangle_data, instancing> isect;
    isect.accept_any_intersection(false);  // closest hit

    auto centerHit = isect.intersect(centerRay, tlas);

    float3 sunDir = normalize(frame.sunDirection.xyz);

    if (centerHit.type == intersection_type::none)
    {
        float3 sky = sky_color(centerDir, sunDir, frame.sunColor.xyz, frame.sunColor.w);
        outHDR.write(float4(sky, 1.0f), gid);
        outAlbedo.write(float4(1.0f), gid);  // sky: albedo = white → remodulation is a no-op
        outNormal.write(float4(0.0f), gid);
        // Write far-plane linear distance so the particle depth test treats sky as
        // "infinitely far".  Using 1.0 (1 m) caused every particle >1 m from the
        // camera to be discarded in RT mode.  The SVGF relative depth check
        // (|cur-prev|/cur > 0.1) still correctly identifies sky/geometry boundaries.
        float skyLinearDepth = frame.proj[3][2] / (1.0f - frame.proj[2][2]);  // = far plane
        outDepth.write(float4(skyLinearDepth, 0.0f, 0.0f, 0.0f), gid);
        outMotion.write(float4(0.0f), gid);
        return;
    }

    SurfaceHit centerSurf = evaluate_surface(
        origin, centerDir, centerHit.distance,
        centerHit.instance_id, centerHit.primitive_id,
        centerHit.triangle_barycentric_coord,
        materials, primitives, textures, texSampler, pixelAngle);

    // ─── Auxiliary outputs (denoiser guide signals) ──────────────────────────

    float linearDepth = length(centerSurf.position - frame.cameraPos.xyz);
    outDepth.write(float4(linearDepth, 0.0f, 0.0f, 0.0f), gid);

    float2 encN = encode_normal(centerSurf.normal);
    outNormal.write(float4(encN * 0.5f + 0.5f, 0.0f, 0.0f), gid);

    float4 clipCurr = frame.viewProj     * float4(centerSurf.position, 1.0f);
    float4 clipPrev = frame.prevViewProj * float4(centerSurf.position, 1.0f);
    float2 uvCurr   = ndc_to_uv(clipCurr.xy / clipCurr.w);
    float2 uvPrev   = ndc_to_uv(clipPrev.xy / clipPrev.w);
    outMotion.write(float4(uvCurr - uvPrev, 0.0f, 0.0f), gid);

    // safeAlbedo from the pixel-centre surface — used for demodulation and
    // written to outAlbedo so rt_remodulate can reconstruct the final colour.
    float3 safeAlbedo = max(centerSurf.albedo, float3(0.01f));
    outAlbedo.write(float4(safeAlbedo, 1.0f), gid);

    // ─── Multi-sample radiance — jittered primary rays ────────────────────────
    // Each sample generates its own primary ray direction by adding a uniform
    // random sub-pixel offset (±0.5 px) to the pixel centre before unprojecting.
    // This samples the scene at a slightly different surface UV each time,
    // eliminating the moiré and texture swimming that occur when every sample
    // always hits the exact same texel.
    //
    // WHY this does not break SVGF:
    //   The G-buffer (above) is always the pixel-centre ray — its depth, normal,
    //   and motion vectors never change between frames for a static scene.
    //   SVGF reprojection therefore sees zero spurious motion, unlike the old
    //   Halton approach which jittered the G-buffer ray deterministically and
    //   produced per-frame motion-vector noise on every static surface.
    //
    //   The radiance samples change frame-to-frame because the RNG seed encodes
    //   frameIndex.  SVGF's temporal accumulator averages these over many frames,
    //   converging to a filtered estimate of the true pixel colour — identical
    //   in principle to TAA, but driven by the path tracer's own sample budget.
    //
    // Demodulation uses safeAlbedo from the pixel-centre hit.  At geometric
    // edges a jittered sample may hit a different surface; the resulting
    // irradiance is slightly mismatched, but SVGF's normal/depth edge-stopping
    // already prevents blurring across surface boundaries so the artefact is
    // imperceptible in practice.
    //
    // RNG seeding: frameIndex × 16 + sampleIdx (×16 > slider max of 4).

    const uint spp = max(rtConst.samplesPerPixel, 1u);
    float3 radianceAccum = float3(0.0f);

    for (uint sampleIdx = 0; sampleIdx < spp; ++sampleIdx)
    {
        RNG rng(gid, uint(frame.frameIndex) * 16u + sampleIdx);

        // ── Jittered primary ray direction ────────────────────────────────────
        // Draw jitter first so bounce sampling uses subsequent, uncorrelated
        // values from the same RNG stream.
        float2 jitter  = rng.next2() - 0.5f;  // uniform [−0.5, +0.5] per axis
        float2 jPixel  = pixel + jitter;
        float2 jUV     = jPixel / float2(screenSize);
        float2 jNDC    = uv_to_ndc(jUV);
        float4 jFar    = frame.invViewProj * float4(jNDC, 1.0f, 1.0f);
        float3 jTarget = jFar.xyz / jFar.w;
        float3 jDir    = normalize(jTarget - origin);  // same origin — perspective rays diverge from camera

        ray jRay;
        jRay.origin       = origin;
        jRay.direction    = jDir;
        jRay.min_distance = 0.001f;
        jRay.max_distance = 1e20f;

        auto jHit = isect.intersect(jRay, tlas);

        if (jHit.type == intersection_type::none)
        {
            // Jittered ray missed — contribute sky radiance for this sample.
            radianceAccum += sky_color(jDir, sunDir, frame.sunColor.xyz, frame.sunColor.w);
            continue;
        }

        SurfaceHit surf = evaluate_surface(
            origin, jDir, jHit.distance,
            jHit.instance_id, jHit.primitive_id,
            jHit.triangle_barycentric_coord,
            materials, primitives, textures, texSampler, pixelAngle);

        float3 N         = surf.normal;
        float3 albedo    = surf.albedo;
        float  roughness = surf.roughness;
        float  metalness = surf.metalness;
        float3 V         = -jDir;

        float3 radiance = float3(0.0f);

        // ── Ambient + emissive ────────────────────────────────────────────────
        radiance += albedo * (frame.ambientColor.xyz + surf.sectorAmbient.xyz);
        radiance += surf.emissive;

        // ── Sun direct ────────────────────────────────────────────────────────
        float NdotSun = max(dot(N, sunDir), 0.0f);
        if (NdotSun > 0.0f)
        {
            ray shadowRay;
            shadowRay.origin       = surf.position + surf.geoNormal * 0.001f;
            shadowRay.direction    = sunDir;
            shadowRay.min_distance = 0.001f;
            shadowRay.max_distance = 1e20f;

            intersector<instancing> shadowIsect;
            shadowIsect.accept_any_intersection(true);
            if (shadowIsect.intersect(shadowRay, tlas).type == intersection_type::none)
            {
                float T = compute_particle_transmittance(
                    shadowRay.origin, sunDir, 1e20f,
                    shadowVolumeCount, shadowVolumes, densityTextures, texSampler);
                float3 sunRadiance = frame.sunColor.xyz * frame.sunColor.w;
                radiance += cook_torrance(N, V, sunDir, albedo, roughness, metalness)
                          * sunRadiance * T;
            }
        }

        // ── Point lights ──────────────────────────────────────────────────────
        uint numLights = min(lightCount, 64u);
        for (uint i = 0; i < numLights; ++i)
        {
            float3 lPos   = lights[i].positionRadius.xyz;
            float  lRad   = lights[i].positionRadius.w;
            float3 lColor = lights[i].colorIntensity.xyz;
            float  lInt   = lights[i].colorIntensity.w;

            float3 toLight = lPos - surf.position;
            float  dist    = length(toLight);
            if (dist > lRad || dist < 0.0001f) continue;

            float3 L     = toLight / dist;
            float  NdotL = max(dot(N, L), 0.0f);
            if (NdotL <= 0.0f) continue;

            float attenuation = lInt / (dist * dist + 0.0001f);
            float falloff     = saturate(1.0f - (dist / lRad));
            attenuation      *= falloff * falloff;

            ray shadowRay;
            shadowRay.origin       = surf.position + surf.geoNormal * 0.001f;
            shadowRay.direction    = L;
            shadowRay.min_distance = 0.001f;
            shadowRay.max_distance = dist - 0.002f;

            intersector<instancing> shadowIsect;
            shadowIsect.accept_any_intersection(true);
            if (shadowIsect.intersect(shadowRay, tlas).type == intersection_type::none)
            {
                float T = compute_particle_transmittance(
                    shadowRay.origin, L, dist - 0.002f,
                    shadowVolumeCount, shadowVolumes, densityTextures, texSampler);
                radiance += cook_torrance(N, V, L, albedo, roughness, metalness)
                          * lColor * attenuation * T;
            }
        }

        // ── Spot lights ───────────────────────────────────────────────────────
        uint numSpots = min(spotLightCount, 16u);
        for (uint si = 0; si < numSpots; ++si)
        {
            float3 sPos     = spotLights[si].positionRange.xyz;
            float  sRange   = spotLights[si].positionRange.w;
            float3 sColor   = spotLights[si].colorIntensity.xyz;
            float  sInt     = spotLights[si].colorIntensity.w;
            float3 sDir     = normalize(spotLights[si].directionOuterCos.xyz);
            float  outerCos = spotLights[si].directionOuterCos.w;
            float  innerCos = spotLights[si].innerCosAndPad.x;

            float3 toLight = sPos - surf.position;
            float  dist    = length(toLight);
            if (dist >= sRange || dist < 0.0001f) continue;

            float3 L = toLight / dist;

            float cosTheta = dot(-L, sDir);
            float cone     = smoothstep(outerCos, innerCos, cosTheta);
            if (cone <= 0.0f) continue;

            float NdotL = max(dot(N, L), 0.0f);
            if (NdotL <= 0.0f) continue;

            float atten = 1.0f - clamp(dist / sRange, 0.0f, 1.0f);
            atten *= atten;

            ray shadowRay;
            shadowRay.origin       = surf.position + surf.geoNormal * 0.001f;
            shadowRay.direction    = L;
            shadowRay.min_distance = 0.001f;
            shadowRay.max_distance = dist - 0.002f;

            intersector<instancing> shadowIsect;
            shadowIsect.accept_any_intersection(true);
            if (shadowIsect.intersect(shadowRay, tlas).type == intersection_type::none)
            {
                float T = compute_particle_transmittance(
                    shadowRay.origin, L, dist - 0.002f,
                    shadowVolumeCount, shadowVolumes, densityTextures, texSampler);
                radiance += cook_torrance(N, V, L, albedo, roughness, metalness)
                          * sColor * sInt * cone * atten * T;
            }
        }

        // ── Particle volume emission (primary hit) ──────────────────────
        // Illuminate this surface from any visible emissive particle density
        // volumes.  Affects all geometry: floor, ceiling, walls, objects.
        radiance += albedo * compute_particle_emission(
            surf.position, N,
            shadowVolumeCount, shadowVolumes, densityTextures, texSampler, tlas);

        // ── Indirect bounces ──────────────────────────────────────────────
        float3 throughput   = float3(1.0f);
        float3 bouncePos    = surf.position;
        float3 bounceGeoN   = surf.geoNormal;
        float3 bounceN      = N;
        float3 bounceAlbedo = albedo;

        for (uint bounce = 0; bounce < rtConst.maxBounces; ++bounce)
        {
            float3 T, B;
            make_basis(bounceN, T, B);
            float3 localDir  = cosine_hemisphere(rng.next2());
            float3 bounceDir = to_world(localDir, T, B, bounceN);

            ray bounceRay;
            bounceRay.origin       = bouncePos + bounceGeoN * 0.001f;
            bounceRay.direction    = bounceDir;
            bounceRay.min_distance = 0.001f;
            bounceRay.max_distance = 1e20f;

            auto bounceHit = isect.intersect(bounceRay, tlas);

            if (bounceHit.type == intersection_type::none)
            {
                float3 sky = sky_color(bounceDir, sunDir,
                                       frame.sunColor.xyz, frame.sunColor.w);
                radiance += throughput * bounceAlbedo * sky;
                break;
            }

            SurfaceHit bounceSurf = evaluate_surface(
                bouncePos + bounceGeoN * 0.001f, bounceDir, bounceHit.distance,
                bounceHit.instance_id, bounceHit.primitive_id,
                bounceHit.triangle_barycentric_coord,
                materials, primitives, textures, texSampler, pixelAngle);

            float3 newN      = bounceSurf.normal;
            float3 newAlbedo = bounceSurf.albedo;

            throughput *= bounceAlbedo;

            if (bounce > 0)
            {
                float p = max(max(throughput.r, throughput.g), throughput.b);
                if (rng.next() > p) break;
                throughput /= p;
            }

            float sunNdotL = max(dot(newN, sunDir), 0.0f);
            if (sunNdotL > 0.0f)
            {
                ray shadowRay;
                shadowRay.origin       = bounceSurf.position + bounceSurf.geoNormal * 0.001f;
                shadowRay.direction    = sunDir;
                shadowRay.min_distance = 0.001f;
                shadowRay.max_distance = 1e20f;

                intersector<instancing> sIsect;
                sIsect.accept_any_intersection(true);
                if (sIsect.intersect(shadowRay, tlas).type == intersection_type::none)
                {
                    float T = compute_particle_transmittance(
                        shadowRay.origin, sunDir, 1e20f,
                        shadowVolumeCount, shadowVolumes, densityTextures, texSampler);
                    float3 sunRad = frame.sunColor.xyz * frame.sunColor.w;
                    radiance += throughput * newAlbedo * sunRad * sunNdotL / PI * T;
                }
            }

            radiance += throughput * bounceSurf.emissive;

            // Particle volume emission at this bounce surface.
            // Carries the same emissive cloud illumination to all geometry
            // reached by indirect paths (floor lit by ceiling bounce, etc.).
            radiance += throughput * newAlbedo * compute_particle_emission(
                bounceSurf.position, newN,
                shadowVolumeCount, shadowVolumes, densityTextures, texSampler, tlas);

            bouncePos    = bounceSurf.position;
            bounceGeoN   = bounceSurf.geoNormal;
            bounceN      = newN;
            bounceAlbedo = newAlbedo;
        }

        radianceAccum += radiance;
    }

    // ─── Write demodulated irradiance + albedo ────────────────────────────────
    // Divide averaged radiance by the pixel-centre safeAlbedo so SVGF denoises
    // pure irradiance.  rt_remodulate multiplies safeAlbedo back in afterward.

    float3 irradiance = radianceAccum / float(spp) / safeAlbedo;

    // ── Firefly suppression ───────────────────────────────────────────────────
    // Bounce rays that escape thin-shell geometry seams (wall-floor, wall-ceiling
    // corners) sample the bright outdoor sky, producing outlier irradiance values
    // amplified up to 1/safeAlbedo × sky_luminance on dark surfaces.  Because the
    // seam geometry is consistent, the same pixel can escape on every frame and
    // SVGF temporal accumulation reinforces rather than suppresses the sparkle.
    // Clamping irradiance here prevents SVGF from locking in the artifact while
    // leaving all legitimate surface lighting unaffected — real indirect
    // contributions on well-lit surfaces are well below the threshold.
    float irrLum = dot(irradiance, float3(0.2126f, 0.7152f, 0.0722f));
    const float kFireflyThreshold = 30.0f;
    if (irrLum > kFireflyThreshold)
        irradiance *= kFireflyThreshold / max(irrLum, 1e-6f);

    outHDR.write(float4(irradiance, 1.0f), gid);
}
