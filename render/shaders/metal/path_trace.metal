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
    sampler texSampler)
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

    // ── Sample textures ──────────────────────────────────────────────────────

    // Albedo: texture × tint.
    float4 albedoSample = textures[mat.albedoTextureIndex].sample(texSampler, uv);
    surf.albedo = albedoSample.rgb * mat.tint.rgb;

    // Normal mapping: decode tangent-space normal, build TBN, transform.
    float3 shadingN = interpN;
    if (mat.normalTextureIndex != 0)
    {
        float3 tangentNormal = textures[mat.normalTextureIndex].sample(texSampler, uv).xyz;
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
        surf.emissive = textures[mat.emissiveTextureIndex].sample(texSampler, uv).rgb;
    else
        surf.emissive = float3(0.0f);

    surf.roughness     = mat.roughness;
    surf.metalness     = mat.metalness;
    surf.sectorAmbient = mat.sectorAmbient;

    return surf;
}

// ─── path_trace_main ─────────────────────────────────────────────────────────

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
    texture2d<float, access::write> outHDR    [[texture(0)]],
    texture2d<float, access::write> outNormal [[texture(1)]],
    texture2d<float, access::write> outDepth  [[texture(2)]],
    texture2d<float, access::write> outMotion [[texture(3)]],
    texture2d<float, access::write> outAlbedo [[texture(4)]],
    array<texture2d<float>, MAX_RT_TEXTURES> textures [[texture(5)]],
    sampler texSampler [[sampler(0)]],
    uint2 gid [[thread_position_in_grid]])
{
    const uint2 screenSize = uint2(frame.screenSize.xy);
    if (gid.x >= screenSize.x || gid.y >= screenSize.y) return;

    RNG rng(gid, uint(frame.frameIndex));

    // ─── Generate primary ray ────────────────────────────────────────────────
    // Jittered pixel centre for temporal accumulation via TAA.
    float2 pixel = float2(gid) + 0.5f + frame.jitter;
    float2 uv    = pixel / float2(screenSize);
    float2 ndc   = uv_to_ndc(uv);

    // Unproject to world-space ray through the inverse VP.
    float4 nearClip = frame.invViewProj * float4(ndc, 0.0f, 1.0f);
    float4 farClip  = frame.invViewProj * float4(ndc, 1.0f, 1.0f);
    float3 origin   = nearClip.xyz / nearClip.w;
    float3 target   = farClip.xyz / farClip.w;
    float3 dir      = normalize(target - origin);

    // ─── Trace primary ray ───────────────────────────────────────────────────

    ray primaryRay;
    primaryRay.origin       = origin;
    primaryRay.direction    = dir;
    primaryRay.min_distance = 0.001f;
    primaryRay.max_distance = 1e20f;

    intersector<triangle_data, instancing> isect;
    isect.accept_any_intersection(false);  // closest hit

    auto hit = isect.intersect(primaryRay, tlas);

    // ─── Miss → sky ──────────────────────────────────────────────────────────

    float3 sunDir = normalize(frame.sunDirection.xyz);

    if (hit.type == intersection_type::none)
    {
        float3 sky = sky_color(dir, sunDir, frame.sunColor.xyz, frame.sunColor.w);
        outHDR.write(float4(sky, 1.0f), gid);
        outAlbedo.write(float4(1.0f), gid);  // sky: albedo = white → remodulation is a no-op
        outNormal.write(float4(0.0f), gid);
        outDepth.write(float4(1.0f, 0.0f, 0.0f, 0.0f), gid);
        outMotion.write(float4(0.0f), gid);
        return;
    }

    // ─── Primary hit — evaluate surface ──────────────────────────────────────

    SurfaceHit surf = evaluate_surface(
        origin, dir, hit.distance,
        hit.instance_id, hit.primitive_id,
        hit.triangle_barycentric_coord,
        materials, primitives, textures, texSampler);

    float3 N         = surf.normal;
    float3 albedo    = surf.albedo;
    float  roughness = surf.roughness;
    float  metalness = surf.metalness;

    // ─── Auxiliary outputs (denoiser guide signals) ──────────────────────────

    float linearDepth = length(surf.position - frame.cameraPos.xyz);
    outDepth.write(float4(linearDepth, 0.0f, 0.0f, 0.0f), gid);

    // Encode normal (world-space → octahedral), remapped to [0,1] for RGBA8Unorm
    // storage (matching the G-buffer convention in gbuffer.metal).
    float2 encN = encode_normal(N);
    outNormal.write(float4(encN * 0.5f + 0.5f, 0.0f, 0.0f), gid);

    // Camera-only motion vectors.
    float4 clipCurr = frame.viewProj     * float4(surf.position, 1.0f);
    float4 clipPrev = frame.prevViewProj * float4(surf.position, 1.0f);
    float2 uvCurr   = ndc_to_uv(clipCurr.xy / clipCurr.w);
    float2 uvPrev   = ndc_to_uv(clipPrev.xy / clipPrev.w);
    outMotion.write(float4(uvCurr - uvPrev, 0.0f, 0.0f), gid);

    // ─── Direct lighting (sun + point lights) ────────────────────────────────

    float3 V = -dir;
    float3 radiance = float3(0.0f);

    // Ambient contribution (per-sector + global).
    radiance += albedo * (frame.ambientColor.xyz + surf.sectorAmbient.xyz);

    // Emissive contribution.
    radiance += surf.emissive;

    // Sun direct + shadow — shadow ray handles occlusion (ceiling/walls block sun
    // indoors automatically; no need for an explicit outdoor flag gate in RT mode).
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
        auto shadowHit = shadowIsect.intersect(shadowRay, tlas);

        if (shadowHit.type == intersection_type::none)
        {
            float3 sunRadiance = frame.sunColor.xyz * frame.sunColor.w;
            radiance += cook_torrance(N, V, sunDir, albedo, roughness, metalness)
                      * sunRadiance;
        }
    }

    // Point lights.
    uint numLights = min(lightCount, 64u);
    for (uint i = 0; i < numLights; ++i)
    {
        float3 lPos   = lights[i].positionRadius.xyz;
        float  lRad   = lights[i].positionRadius.w;
        float3 lColor = lights[i].colorIntensity.xyz;
        float  lInt   = lights[i].colorIntensity.w;

        float3 toLight = lPos - surf.position;
        float  dist    = length(toLight);
        if (dist > lRad || dist < 0.0001f) continue;  // skip degenerate / inside-light

        float3 L     = toLight / dist;
        float  NdotL = max(dot(N, L), 0.0f);
        if (NdotL <= 0.0f) continue;

        // Attenuation: inverse-square with radius falloff.
        float attenuation = lInt / (dist * dist + 0.0001f);
        float falloff     = saturate(1.0f - (dist / lRad));
        attenuation      *= falloff * falloff;

        // Shadow ray.
        ray shadowRay;
        shadowRay.origin       = surf.position + surf.geoNormal * 0.001f;
        shadowRay.direction    = L;
        shadowRay.min_distance = 0.001f;
        shadowRay.max_distance = dist - 0.002f;

        intersector<instancing> shadowIsect;
        shadowIsect.accept_any_intersection(true);
        auto shadowHit = shadowIsect.intersect(shadowRay, tlas);

        if (shadowHit.type == intersection_type::none)
        {
            radiance += cook_torrance(N, V, L, albedo, roughness, metalness)
                      * lColor * attenuation;
        }
    }

    // Spot lights.
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
        if (dist >= sRange || dist < 0.0001f) continue;  // skip degenerate / inside-light

        float3 L = toLight / dist;

        // Cone falloff.
        float cosTheta = dot(-L, sDir);
        float cone     = smoothstep(outerCos, innerCos, cosTheta);
        if (cone <= 0.0f) continue;

        float NdotL = max(dot(N, L), 0.0f);
        if (NdotL <= 0.0f) continue;

        // Distance attenuation.
        float atten = 1.0f - clamp(dist / sRange, 0.0f, 1.0f);
        atten *= atten;

        // Shadow ray.
        ray shadowRay;
        shadowRay.origin       = surf.position + surf.geoNormal * 0.001f;
        shadowRay.direction    = L;
        shadowRay.min_distance = 0.001f;
        shadowRay.max_distance = dist - 0.002f;

        intersector<instancing> shadowIsect;
        shadowIsect.accept_any_intersection(true);
        auto shadowHit = shadowIsect.intersect(shadowRay, tlas);

        if (shadowHit.type == intersection_type::none)
        {
            radiance += cook_torrance(N, V, L, albedo, roughness, metalness)
                      * sColor * sInt * cone * atten;
        }
    }

    // ─── Indirect bounces ────────────────────────────────────────────────────

    float3 throughput  = float3(1.0f);
    float3 bouncePos   = surf.position;
    float3 bounceGeoN  = surf.geoNormal;
    float3 bounceN     = N;
    float3 bounceAlbedo = albedo;

    for (uint bounce = 0; bounce < rtConst.maxBounces; ++bounce)
    {
        // Cosine-weighted hemisphere sample.
        float3 T, B;
        make_basis(bounceN, T, B);
        float3 localDir = cosine_hemisphere(rng.next2());
        float3 bounceDir = to_world(localDir, T, B, bounceN);

        ray bounceRay;
        bounceRay.origin       = bouncePos + bounceGeoN * 0.001f;
        bounceRay.direction    = bounceDir;
        bounceRay.min_distance = 0.001f;
        bounceRay.max_distance = 1e20f;

        auto bounceHit = isect.intersect(bounceRay, tlas);

        if (bounceHit.type == intersection_type::none)
        {
            // Sky contribution.
            float3 sky = sky_color(bounceDir, sunDir,
                                   frame.sunColor.xyz, frame.sunColor.w);
            // Cosine-weighted PDF cancels with the NdotL in the rendering equation,
            // leaving just albedo/PI * PI = albedo.  (Hemisphere integral of cos/PI = 1.)
            radiance += throughput * bounceAlbedo * sky;
            break;
        }

        // Hit surface — evaluate with texture sampling.
        SurfaceHit bounceSurf = evaluate_surface(
            bouncePos + bounceGeoN * 0.001f, bounceDir, bounceHit.distance,
            bounceHit.instance_id, bounceHit.primitive_id,
            bounceHit.triangle_barycentric_coord,
            materials, primitives, textures, texSampler);

        float3 newN      = bounceSurf.normal;
        float3 newAlbedo = bounceSurf.albedo;

        // Accumulate throughput (lambertian: albedo / PI * PI = albedo).
        throughput *= bounceAlbedo;

        // Russian roulette after first bounce.
        if (bounce > 0)
        {
            float p = max(max(throughput.r, throughput.g), throughput.b);
            if (rng.next() > p) break;
            throughput /= p;
        }

        // Direct lighting at the bounce hit — shadow ray handles indoor occlusion.
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
            auto sHit = sIsect.intersect(shadowRay, tlas);

            if (sHit.type == intersection_type::none)
            {
                float3 sunRad = frame.sunColor.xyz * frame.sunColor.w;
                radiance += throughput * newAlbedo * sunRad * sunNdotL / PI;
            }
        }

        // Emissive at bounce.
        radiance += throughput * bounceSurf.emissive;

        bouncePos    = bounceSurf.position;
        bounceGeoN   = bounceSurf.geoNormal;
        bounceN      = newN;
        bounceAlbedo = newAlbedo;
    }

    // ─── Write demodulated irradiance + albedo ────────────────────────────────────
    // Divide out primary-hit albedo so the SVGF denoiser operates on lighting
    // only.  A post-denoiser remodulation pass multiplies albedo back in.
    //
    // WHY 0.01 and not 0.1:
    // The floor value is the threshold below which an albedo channel is clamped.
    // The floor in BOTH demodulation and remodulation must be identical so they
    // cancel: irradiance = radiance / safeAlbedo,  final = irradiance * safeAlbedo.
    //
    // With floor=0.1, any surface darker than albedo≈0.1 (e.g. the floor at ~0.02–
    // 0.05) had ALL its channels clamped to the same flat 0.1.  Two adjacent dark
    // pixels with different actual albedos (brick vs mortar) both got safeAlbedo=0.1
    // → their demodulated irradiance differed (0.02L/0.1 ≠ 0.05L/0.1), causing false
    // irradiance edges that SVGF couldn't preserve.  After remodulation by the same
    // flat 0.1 the edge collapsed to a uniform value → texture destroyed.
    //
    // With floor=0.01, the floor texture (albedo ~0.02–0.05) is NOT clamped at all:
    // safeAlbedo = actual albedo.  Demodulation then becomes proper per-channel
    // cancellation (0.02L / 0.02 = L, 0.05L / 0.05 = L) → the demodulated irradiance
    // is UNIFORM across uniformly-lit floor regions regardless of texture variation.
    // SVGF can blur freely without creating artefacts; remodulation with the stored
    // safeAlbedo (= actual albedo) restores the texture automatically.
    //
    // 0.01 caps the maximum per-channel noise amplification at 100× (vs 1000× for
    // 0.001, which caused blow-out on near-zero colour channels of saturated darks).

    float3 safeAlbedo = max(albedo, float3(0.01f));
    outHDR.write(float4(radiance / safeAlbedo, 1.0f), gid);
    outAlbedo.write(float4(safeAlbedo, 1.0f), gid);
}
