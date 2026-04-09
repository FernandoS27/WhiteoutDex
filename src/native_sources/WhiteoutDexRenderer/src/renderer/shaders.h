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
    float4   MaterialFlags;   // .x=unshaded, .y=constantColor
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

    // Texture animation: apply UV tiling + offset
    float2 uv = input.UV;
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
    float4   MaterialFlags;   // .x=unshaded, .y=constantColor
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
    float  finalAlpha = texColor.a * vertColor.a * geosetAlpha;

    // Alpha test
    float alphaRef = AmbientColor.a;
    if (alphaRef > 0.0 && finalAlpha < alphaRef)
        clip(-1);

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

// ============================================================================
// Geoset pixel shader — in-shader multi-layer compositing via Texture2DArray
// Used only by RenderGeosets(). Particles/ribbons/viewcube use g_pixelShaderSrc.
// ============================================================================
static const char* g_geosetPixelShaderSrc = R"HLSL(
cbuffer CBPerFrame : register(b0) {
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
    float4   LightDir;
    float4   LightColor;
    float4   AmbientColor;
    float4   ExtraParams;     // .x=geosetAlpha, .yzw=colorTint RGB
    float4   TexAnimParams;   // unused in geoset path
    float4   MaterialFlags;   // unused in geoset path
};

#define MAX_LAYERS 8

cbuffer CBLayers : register(b1) {
    int4   LayerCount;                  // .x = numLayers
    float4 LayerParams[MAX_LAYERS];     // .x=filterMode, .y=alpha, .z=unshaded, .w=constantColor
    float4 LayerTexAnim[MAX_LAYERS];    // .x=uOff, .y=vOff, .z=uTile, .w=vTile
};

Texture2DArray texLayers : register(t0);
SamplerState   samLinear : register(s0);

struct PS_INPUT {
    float4 Pos      : SV_POSITION;
    float3 Normal   : NORMAL;
    float4 Color    : COLOR;
    float2 UV       : TEXCOORD0;
    float3 WorldPos : TEXCOORD1;
};

float4 SampleLayer(PS_INPUT input, int li, float3 lighting, float3 colorTint) {
    float4 p  = LayerParams[li];
    float4 ta = LayerTexAnim[li];
    float uTile = (ta.z == 0.0) ? 1.0 : ta.z;
    float vTile = (ta.w == 0.0) ? 1.0 : ta.w;
    float2 uv = input.UV * float2(uTile, vTile) + float2(ta.x, ta.y);

    float4 tex = texLayers.Sample(samLinear, float3(uv, (float)li));

    bool unshaded   = p.z > 0.5;
    bool constColor = p.w > 0.5;
    float3 light    = unshaded   ? float3(1,1,1) : lighting;
    float3 tint     = constColor ? float3(1,1,1) : colorTint;

    float3 rgb   = light * tex.rgb * input.Color.rgb * tint;
    float  alpha = tex.a * input.Color.a * p.y;
    return float4(rgb, alpha);
}

float4 PSGeoset(PS_INPUT input) : SV_TARGET {
    float3 N = normalize(input.Normal);
    float3 L = normalize(-LightDir.xyz);
    float  NdotL = saturate(dot(N, L));
    float3 diffuse = LightColor.rgb * NdotL;
    float3 ambient = AmbientColor.rgb;
    float3 lighting = ambient + diffuse;

    float  geosetAlpha = ExtraParams.x;
    float3 colorTint   = ExtraParams.yzw;
    if (geosetAlpha < 0.004) clip(-1);

    int numLayers = LayerCount.x;
    if (numLayers <= 0) { clip(-1); return float4(0,0,0,0); }

    // Layer 0 — base. Hardware blend uses layer 0's filter mode (set CPU-side).
    float4 accum = SampleLayer(input, 0, lighting, colorTint);
    int baseFilter = (int)LayerParams[0].x;

    // Alpha test on layer 0 when FILTER_TRANSPARENT (==1)
    if (baseFilter == 1) {
        if (accum.a < 0.745) clip(-1);
    }

    // Composite layers 1..N-1 into accum in-shader (matches per-layer blend modes).
    [loop]
    for (int li = 1; li < numLayers; ++li) {
        float4 s = SampleLayer(input, li, lighting, colorTint);
        int fm = (int)LayerParams[li].x;
        if (fm == 0) {                      // NONE: opaque replace
            accum.rgb = s.rgb;
            accum.a   = s.a;
        } else if (fm == 1) {               // TRANSPARENT: replace where src alpha passes
            if (s.a >= 0.745) {
                accum.rgb = s.rgb;
                accum.a   = s.a;
            }
        } else if (fm == 2) {               // BLEND: lerp by src alpha
            accum.rgb = lerp(accum.rgb, s.rgb, s.a);
        } else if (fm == 3) {               // ADDITIVE: add src.rgb
            accum.rgb += s.rgb;
        } else if (fm == 4) {               // ADD_ALPHA: add src.rgb * src.a
            accum.rgb += s.rgb * s.a;
        } else if (fm == 5) {               // MODULATE: multiply
            accum.rgb *= s.rgb;
        } else if (fm == 6) {               // MODULATE_2X: 2 * dst * src
            accum.rgb = 2.0 * accum.rgb * s.rgb;
        }
    }

    // Apply per-geoset fade (0..1)
    accum.a *= geosetAlpha;
    return accum;
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
