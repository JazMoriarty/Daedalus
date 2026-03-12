// common.h — shared MSL types and utilities.
// Include this in every .metal file via: #include "common.h"
//
// Coordinate convention: left-handed, Y-up, depth [0,1] (Metal NDC).
// UV (0,0) = top-left.

#pragma once

#include <metal_stdlib>
using namespace metal;

// ─── GPU constant-buffer structs ──────────────────────────────────────────────
// Must match daedalus/render/scene_data.h exactly.

struct FrameConstants      // buffer(0), vertex + fragment + compute
{
    float4x4 view;
    float4x4 proj;
    float4x4 viewProj;
    float4x4 invViewProj;
    float4x4 prevViewProj;
    float4x4 sunViewProj;
    float4x4 mirrorViewProj;  // reflected-camera VP for projective mirror UV (G-buffer pass)

    float4 cameraPos;     // w unused
    float4 cameraDir;     // w unused
    float4 screenSize;    // xy = pixel dims, zw = 1/pixel dims

    float4 sunDirection;  // w unused
    float4 sunColor;      // w = intensity
    float4 ambientColor;  // w unused

    float  time;
    float  deltaTime;
    float  frameIndex;
    float  pad0;

    float2 jitter;
    float2 pad1;
};

struct ModelConstants      // buffer(1), vertex
{
    float4x4 model;
    float4x4 normalMat;
    float4x4 prevModel;
};

struct PointLightGPU
{
    float4 positionRadius;  // xyz = world position, w = radius
    float4 colorIntensity;  // xyz = colour, w = intensity
};

struct SpotLightGPU       // buffer(3) in lighting compute, 64 bytes
{
    float4 positionRange;      // xyz = world pos,     w = range
    float4 directionOuterCos;  // xyz = normalised dir, w = cos(outerConeAngle)
    float4 colorIntensity;     // xyz = colour,         w = intensity
    float4 innerCosAndPad;     // x   = cos(innerConeAngle), yzw = 0
};

struct DecalConstants      // buffer(1) in decal vertex + fragment, 144 bytes
{
    float4x4 model;      // local unit-cube → world  (vertex: positions the OBB)
    float4x4 invModel;   // world → local unit-cube  (fragment: bounds test + UV)
    float    roughness;
    float    metalness;
    float    opacity;
    float    pad;
};

struct MaterialConstants   // buffer(1) in G-buffer + transparent fragment, 64 bytes
{
    float  roughness;
    float  metalness;
    float  isMirrorSurface;  // 1.0 = sample emissive via projective mirror VP; 0.0 = standard
    float  pad1;
    float4 tint;          ///< Albedo tint (rgb) + opacity override (a); default = (1,1,1,1).
    float2 uvOffset;      ///< UV origin of the active sprite sheet frame cell; default = (0,0).
    float2 uvScale;       ///< UV size of one frame cell; default = (1,1).
    float4 sectorAmbient; ///< xyz = per-sector ambient color × intensity; baked into emissive.
};

// ─── Helpers ─────────────────────────────────────────────────────────────────

/// Reconstruct world-space position from NDC depth and screen UV.
inline float3 reconstruct_world_pos(float depth, float2 uv, float4x4 invViewProj)
{
    float2 ndc    = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldPos = invViewProj * clipPos;
    return worldPos.xyz / worldPos.w;
}

/// Octahedral normal encoding — maps unit sphere to [-1,1]^2.
inline float2 encode_normal(float3 n)
{
    float2 p = n.xy / (abs(n.x) + abs(n.y) + abs(n.z));
    return (n.z <= 0.0) ? ((1.0 - abs(p.yx)) * sign(p)) : p;
}

inline float3 decode_normal(float2 e)
{
    float3 v = float3(e, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0) v.xy = (1.0 - abs(v.yx)) * sign(v.xy);
    return normalize(v);
}

/// UV → NDC (Metal: y flipped)
inline float2 uv_to_ndc(float2 uv)
{
    return uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
}

/// NDC → UV
inline float2 ndc_to_uv(float2 ndc)
{
    return ndc * float2(0.5, -0.5) + 0.5;
}

// ─── PBR helpers ─────────────────────────────────────────────────────────────

constant float PI = 3.14159265359;

/// GGX/Trowbridge-Reitz distribution
inline float distribution_ggx(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

/// Smith-Schlick geometry term
inline float geometry_smith(float NdotV, float NdotL, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float ggx1 = NdotV / (NdotV * (1.0 - k) + k);
    float ggx2 = NdotL / (NdotL * (1.0 - k) + k);
    return ggx1 * ggx2;
}

/// Schlick Fresnel
inline float3 fresnel_schlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

/// Cook-Torrance specular BRDF
inline float3 cook_torrance(float3 N, float3 V, float3 L,
                             float3 albedo, float roughness, float metalness)
{
    float3 F0 = mix(float3(0.04), albedo, metalness);
    float3 H  = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float  D = distribution_ggx(NdotH, roughness);
    float  G = geometry_smith(NdotV, NdotL, roughness);
    float3 F = fresnel_schlick(HdotV, F0);

    float3 numerator   = D * G * F;
    float  denominator = 4.0 * NdotV * NdotL + 0.0001;
    float3 specular    = numerator / denominator;

    float3 kD = (1.0 - F) * (1.0 - metalness);
    return (kD * albedo / PI + specular) * NdotL;
}

// ─── Particle GPU structs ──────────────────────────────────────────────────────────────────────────────────────────────────
// Must match scene_data.h exactly.

struct ParticleGPU                  // 64 bytes per particle
{
    // packed_float3 = 12 bytes (matches glm::vec3); plain float3 would be 16
    // and break the C++ / MSL struct layout match.
    packed_float3 pos;
    float         life;
    packed_float3 vel;
    float         maxLife;
    float4 color;     // HDR tint (rgb) + opacity (a)
    float  size;
    float  rot;
    uint   frameIdx;
    uint   flags;     // bit 0 = alive
};

struct ParticleEmitterConstants     // buffer(0) in all particle shaders, 160 bytes
{   // packed_float3 = 12 bytes; matches glm::vec3 on the C++ side.
    packed_float3 emitterPos;
    float         emissionRate;

    packed_float3 emitDir;
    float         coneHalfAngle;

    float  speedMin;
    float  speedMax;
    float  lifetimeMin;
    float  lifetimeMax;

    float4 colorStart;
    float4 colorEnd;

    float  sizeStart;
    float  sizeEnd;
    float  drag;
    float  turbulenceScale;

    packed_float3 gravity;
    float         emissiveScale;

    uint   maxParticles;
    uint   spawnThisFrame;
    uint   frameIndex;
    uint   aliveListFlip;   // 0 = read A write B, 1 = read B write A

    float2 atlasSize;       // (cols, rows)
    float  atlasFrameRate;
    float  velocityStretch;

    float  softRange;
    float  pad0;
    float  pad1;
    float  pad2;
};

struct DrawIndirectArgs              // 16 bytes — matches MTLDrawPrimitivesIndirectArguments
{
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint baseInstance;
};

// ─── Curl noise helpers ─────────────────────────────────────────────────────────────────────────────────────────────────

/// Value noise hash for a 3-component integer seed.
inline float hash31(uint3 s)
{
    s = s * uint3(1597334673u, 3812015801u, 2798796415u);
    uint v = (s.x ^ s.y ^ s.z) * 1597334673u;
    return float(v) * (1.0 / float(0xffffffffu));
}

/// Gradient noise for a float3 input.
inline float grad_noise(float3 p)
{
    uint3 i  = uint3(floor(p));
    float3 f = fract(p);
    float3 u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(mix(hash31(i),             hash31(i + uint3(1,0,0)), u.x),
            mix(hash31(i + uint3(0,1,0)), hash31(i + uint3(1,1,0)), u.x), u.y),
        mix(mix(hash31(i + uint3(0,0,1)), hash31(i + uint3(1,0,1)), u.x),
            mix(hash31(i + uint3(0,1,1)), hash31(i + uint3(1,1,1)), u.x), u.y),
        u.z) * 2.0 - 1.0;
}

/// Analytically correct curl noise — divergence-free 3D velocity field.
/// Produces swirling organic motion with no artificial sinks or sources.
inline float3 curl_noise(float3 p)
{
    const float e = 0.0001;
    float3 dx = float3(e, 0, 0);
    float3 dy = float3(0, e, 0);
    float3 dz = float3(0, 0, e);
    // Potential vector field (Phi_x, Phi_y, Phi_z)
    float x0 = grad_noise(p + dy) - grad_noise(p - dy);
    float x1 = grad_noise(p + dz) - grad_noise(p - dz);
    float y0 = grad_noise(p + dz) - grad_noise(p - dz);
    float y1 = grad_noise(p + dx) - grad_noise(p - dx);
    float z0 = grad_noise(p + dx) - grad_noise(p - dx);
    float z1 = grad_noise(p + dy) - grad_noise(p - dy);
    return float3(x0 - x1, y0 - y1, z0 - z1) / (2.0 * e);
}

// ─── SSR constants ─────────────────────────────────────────────────────────────────────────────────────────────
// Must match daedalus/render/scene_data.h SSRConstantsGPU exactly.

struct SSRConstants   // buffer(1) in ssr_main, 32 bytes
{
    float maxDistance;      // max ray march distance (metres)
    float thickness;        // depth intersection tolerance (metres)
    float roughnessCutoff;  // skip SSR above this roughness
    float fadeStart;        // screen-edge UV distance to begin fading
    uint  maxSteps;         // maximum ray march iterations
    float pad0;
    float pad1;
    float pad2;
};

// ─── Volumetric fog structs and helpers ─────────────────────────────────────────────────────────────────────────────────────────────
// Must match daedalus/render/scene_data.h VolumetricFogConstantsGPU exactly.

struct VolumetricFogConstants   // buffer(1) in all fog compute shaders, 32 bytes
{
    float  density;     // extinction coefficient (1/m)
    float  anisotropy;  // Henyey-Greenstein g factor (-1..1; 0 = isotropic)
    float  scattering;  // single-scatter albedo (0..1)
    float  fogFar;      // far depth limit for the froxel volume (metres)
    float4 ambientFog;  // xyz = ambient in-scatter colour; w = fogNear (metres)
};

// Froxel volume dimensions: fixed-size grid covering the view frustum.
// 160×90×64 ≈ 921k voxels; each RGBA16F slice is ~7.4 MB total per 3D texture.
constant uint FROXEL_W = 160;
constant uint FROXEL_H = 90;
constant uint FROXEL_D = 64;

/// Henyey-Greenstein phase function.
/// @param cosTheta  Cosine of the angle between view direction and light-to-sample direction.
/// @param g         Anisotropy: 0 = isotropic, >0 = forward scatter, <0 = backward scatter.
inline float hg_phase(float cosTheta, float g)
{
    float g2    = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f - g2) / (4.0f * PI * pow(max(denom, 1e-6f), 1.5f));
}

/// Convert Metal NDC depth [0,1] to linear view-space depth (metres along the view axis).
/// Uses the LH perspective-ZO inverse:
///   ndcZ = proj[2][2] + proj[3][2] / viewZ  ⇒  viewZ = proj[3][2] / (ndcZ - proj[2][2])
/// This is the exact inverse of glm::perspectiveLH_ZO.
inline float ndc_to_linear_depth(float ndcZ, float4x4 proj)
{
    // proj[col][row] in MSL (column-major) — same layout as GLM.
    // proj[2][2] = far/(far-near),  proj[3][2] = -far*near/(far-near)
    return proj[3][2] / (ndcZ - proj[2][2]);
}

// ─── DoF constants ────────────────────────────────────────────────────────────────────────────────────────────────
// Must match daedalus/render/scene_data.h DoFConstantsGPU exactly.

struct DoFConstants   // buffer(1) in dof_coc, dof_blur, dof_composite, 32 bytes
{
    float focusDistance;   // world-space focus plane distance (metres)
    float focusRange;      // in-focus band depth (metres)
    float bokehRadius;     // maximum blur radius (pixels)
    float nearTransition;  // near-field ramp distance (metres)
    float farTransition;   // far-field ramp distance (metres)
    float pad0;
    float pad1;
    float pad2;
};

// ─── Motion blur constants ──────────────────────────────────────────────────────────────────────────────────────
// Must match daedalus/render/scene_data.h MotionBlurConstantsGPU exactly.

struct MotionBlurConstants   // buffer(1) in motion_blur_main, 16 bytes
{
    float shutterAngle;  // fraction of frame time the shutter is open (0..1)
    uint  numSamples;    // number of velocity-direction samples
    float pad0;
    float pad1;
};

// ─── Colour grading constants ─────────────────────────────────────────────────────────────────────────────
// Must match daedalus/render/scene_data.h ColorGradingConstantsGPU exactly.

struct ColorGradingConstants   // buffer(1) in color_grade_frag, 16 bytes
{
    float intensity;  // LUT blend weight (0 = passthrough, 1 = full LUT)
    float pad0;
    float pad1;
    float pad2;
};

// ─── Optional FX constants ──────────────────────────────────────────────────
// Must match daedalus/render/scene_data.h OptionalFxConstantsGPU exactly.

struct OptionalFxConstants   // buffer(1) in optional_fx_frag, 32 bytes
{
    float caAmount;           // chromatic aberration radius (0 = off)
    float vignetteIntensity;  // vignette darkening strength (0..1)
    float vignetteRadius;     // vignette inner edge in UV² (0..1; lower = larger vignette)
    float grainAmount;        // film grain amplitude (0 = off)
    float grainSeed;          // frame-varying seed for temporal variation
    float pad0;
    float pad1;
    float pad2;
};

// ─── Luminance / colour utilities ───────────────────────────────────────────────────────────────────────────────

inline float luminance(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

// ─── ACES tone mapping ───────────────────────────────────────────────────────

inline float3 aces_film(float3 x)
{
    float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}
