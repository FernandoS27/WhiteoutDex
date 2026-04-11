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
    bool isConstColor   = MaterialFlags.y > 0.5;

    // Unshaded: skip lighting, use full-bright texture
    float3 lighting = isUnshaded ? float3(1, 1, 1) : (ambient + diffuse);

    // ConstantColor: skip geoset color tint (Wc3VertexMod animation)
    float3 tint = isConstColor ? float3(1, 1, 1) : colorTint;

    float3 finalColor = lighting * texColor.rgb * vertColor.rgb * tint;

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
