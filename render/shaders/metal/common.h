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

struct MaterialConstants   // buffer(1) in G-buffer fragment, 16 bytes
{
    float roughness;
    float metalness;
    float pad0;
    float pad1;
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

// ─── Luminance / colour utilities ───────────────────────────────────────────

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
