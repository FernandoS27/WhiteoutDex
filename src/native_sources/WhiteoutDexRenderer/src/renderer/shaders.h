#pragma once
// ============================================================================
// WhiteoutDex Real-Time Renderer — Embedded HLSL Shaders
// Compiled at runtime via D3DCompile — no external .hlsl files needed
// ============================================================================

static const char* g_vertexShaderSrc = R"HLSL(
cbuffer CBPerFrame : register(b0) {
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
    float4   LightDir;
    float4   LightColor;
    float4   AmbientColor;
    float4   ExtraParams;     // .x=geosetAlpha, .yzw=colorTint RGB
    float4   TexAnimParams;   // .x=uOff, .y=vOff, .z=uTile, .w=vTile
    float4   MaterialFlags;   // .x=unshaded, .y=constantColor, .z=texRotation
};

struct VS_INPUT {
    float3 Pos    : POSITION;
    float3 Normal : NORMAL;
    float4 Color  : COLOR;
    float2 UV     : TEXCOORD0;
};

struct PS_INPUT {
    float4 Pos      : SV_POSITION;
    float3 Normal   : NORMAL;
    float4 Color    : COLOR;
    float2 UV       : TEXCOORD0;
    float3 WorldPos : TEXCOORD1;
};

PS_INPUT VSMain(VS_INPUT input) {
    PS_INPUT output;

    float4 worldPos = mul(float4(input.Pos, 1.0), World);
    float4 viewPos  = mul(worldPos, View);
    output.Pos      = mul(viewPos, Projection);

    output.Normal   = normalize(mul(input.Normal, (float3x3)World));
    output.Color    = input.Color;

    // Texture animation: apply rotation + tiling + offset
    float2 uv = input.UV;
    float texRot = MaterialFlags.z;
    if (texRot != 0.0) {
        float2 center = float2(0.5, 0.5);
        float2 rel = uv - center;
        float c = cos(texRot);
        float s = sin(texRot);
        uv = float2(c * rel.x - s * rel.y, s * rel.x + c * rel.y) + center;
    }
    float uTile = TexAnimParams.z;
    float vTile = TexAnimParams.w;
    if (uTile != 0.0 || vTile != 0.0)
        uv = uv * float2(uTile, vTile) + float2(TexAnimParams.x, TexAnimParams.y);
    output.UV = uv;

    output.WorldPos = worldPos.xyz;

    return output;
}
)HLSL";

static const char* g_pixelShaderSrc = R"HLSL(
cbuffer CBPerFrame : register(b0) {
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
    float4   LightDir;
    float4   LightColor;
    float4   AmbientColor;
    float4   ExtraParams;
    float4   TexAnimParams;
    float4   MaterialFlags;   // .x=unshaded, .y=constantColor, .z=texRotation
};

Texture2D    texDiffuse : register(t0);
SamplerState samLinear  : register(s0);

struct PS_INPUT {
    float4 Pos      : SV_POSITION;
    float3 Normal   : NORMAL;
    float4 Color    : COLOR;
    float2 UV       : TEXCOORD0;
    float3 WorldPos : TEXCOORD1;
};

float4 PSMain(PS_INPUT input) : SV_TARGET {
    float3 N = normalize(input.Normal);
    float3 L = normalize(-LightDir.xyz);

    float NdotL = saturate(dot(N, L));
    float3 diffuse = LightColor.rgb * NdotL;
    float3 ambient = AmbientColor.rgb;

    float4 texColor = texDiffuse.Sample(samLinear, input.UV);
    float4 vertColor = input.Color;

    // Geoset alpha (ExtraParams.x) + color tint (ExtraParams.yzw)
    float geosetAlpha = ExtraParams.x;
    float3 colorTint  = ExtraParams.yzw;

    // Alpha test against texture alpha only (not affected by opacity fade)
    float texAlpha = texColor.a * vertColor.a;
    float alphaRef = AmbientColor.a;
    if (alphaRef > 0.0 && texAlpha < alphaRef)
        clip(-1);

    // Apply opacity after alpha test
    float finalAlpha = texAlpha * geosetAlpha;

    if (geosetAlpha < 0.004)
        clip(-1);

    // Material flags from Wc3Material
    bool isUnshaded     = MaterialFlags.x > 0.5;

    // Unshaded: skip lighting, use full-bright texture
    float3 lighting = isUnshaded ? float3(1, 1, 1) : (ambient + diffuse);

    float3 finalColor = lighting * texColor.rgb * vertColor.rgb * colorTint;

    return float4(finalColor, finalAlpha);
}
)HLSL";

// Grid shader — simple unlit lines
static const char* g_lineVertexShaderSrc = R"HLSL(
cbuffer CBPerFrame : register(b0) {
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
    float4   LightDir;
    float4   LightColor;
    float4   AmbientColor;
    float4   ExtraParams;
    float4   TexAnimParams;
    float4   MaterialFlags;
};

struct VS_INPUT {
    float3 Pos   : POSITION;
    float4 Color : COLOR;
};

struct PS_INPUT {
    float4 Pos   : SV_POSITION;
    float4 Color : COLOR;
};

PS_INPUT VSLine(VS_INPUT input) {
    PS_INPUT output;
    float4 worldPos = mul(float4(input.Pos, 1.0), World);
    float4 viewPos  = mul(worldPos, View);
    output.Pos      = mul(viewPos, Projection);
    output.Color    = input.Color;
    return output;
}
)HLSL";

static const char* g_linePixelShaderSrc = R"HLSL(
struct PS_INPUT {
    float4 Pos   : SV_POSITION;
    float4 Color : COLOR;
};

float4 PSLine(PS_INPUT input) : SV_TARGET {
    return input.Color;
}
)HLSL";

// ============================================================================
// Compute shader: GPU vertex skinning
// Each thread skins one vertex: position (4x4) + normal (3x3), 4 influences.
// ============================================================================
static const char* g_skinComputeShaderSrc = R"HLSL(

// Matches the C++ Vertex struct: pos(3) + normal(3) + color(4) + uv(2) = 12 floats = 48 bytes
struct Vertex {
    float3 position;
    float3 normal;
    float4 color;
    float2 uv;
};

// Per-vertex bone influences (4 indices + 4 weights)
struct VertexWeight {
    uint4  boneIdx;
    float4 weight;
};

// Node palette — offset matrices (invBind * current), one per hierarchy node
StructuredBuffer<float4x4>   NodePalette : register(t0);
StructuredBuffer<Vertex>     BaseVerts   : register(t1);
StructuredBuffer<VertexWeight> Weights   : register(t2);

RWStructuredBuffer<Vertex>   OutVerts    : register(u0);

[numthreads(256, 1, 1)]
void CSSkin(uint3 dtid : SV_DispatchThreadID) {
    uint idx = dtid.x;

    // Bounds check (dispatch may overshoot)
    uint vertCount, stride;
    BaseVerts.GetDimensions(vertCount, stride);
    if (idx >= vertCount) return;

    Vertex base = BaseVerts[idx];
    VertexWeight w = Weights[idx];

    float3 posSum = float3(0, 0, 0);
    float3 nrmSum = float3(0, 0, 0);
    float totalW  = 0;

    uint nodeCount, nodeStride;
    NodePalette.GetDimensions(nodeCount, nodeStride);

    uint bones[4] = { w.boneIdx.x, w.boneIdx.y, w.boneIdx.z, w.boneIdx.w };
    float wts[4]  = { w.weight.x,  w.weight.y,  w.weight.z,  w.weight.w  };

    [unroll]
    for (int j = 0; j < 4; j++) {
        float bw = wts[j];
        if (bw < 0.0001) continue;
        if (bones[j] >= nodeCount) continue;

        float4x4 mat = NodePalette[bones[j]];

        // Position: full 4x4 transform
        // StructuredBuffer<float4x4> stores row-major bytes but HLSL reads
        // them as column-major, effectively transposing the matrix.
        // Use mul(M, v) instead of mul(v, M) to compensate.
        posSum += bw * mul(mat, float4(base.position, 1.0)).xyz;

        // Normal: 3x3 rotation only (no translation)
        nrmSum += bw * mul((float3x3)mat, base.normal);

        totalW += bw;
    }

    Vertex result;
    if (totalW < 0.0001) {
        result = base;          // no valid weights — passthrough
    } else {
        result.position = posSum;
        result.normal   = normalize(nrmSum);
        result.color    = base.color;
        result.uv       = base.uv;
    }

    OutVerts[idx] = result;
}
)HLSL";
