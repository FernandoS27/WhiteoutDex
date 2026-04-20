// ============================================================================
// WhiteoutDex Renderer — Mesh / Particle / Ribbon shaders
// Vertex shader: WVP transform + texture animation
// Pixel shader:  Directional lighting, alpha test, geoset opacity
// ============================================================================

import common;

ConstantBuffer<PerFrameData> cb;

Texture2D    texDiffuse;
SamplerState samLinear;

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD0;
}

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal   : NORMAL;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
}

// ---- Vertex Shader ----

[shader("vertex")]
PSInput vsMain(VSInput input)
{
    PSInput output;

    let worldPos = mul(float4(input.position, 1.0), cb.world);
    let viewPos  = mul(worldPos, cb.view);
    output.position = mul(viewPos, cb.projection);

    output.normal   = normalize(mul(input.normal, (float3x3)cb.world));
    output.color    = input.color;
    output.uv       = applyTextureAnimation(input.uv, cb.materialFlags.z, cb.texAnimParams);
    output.worldPos = worldPos.xyz;

    return output;
}

// ---- Pixel Shader ----

[shader("fragment")]
float4 psMain(PSInput input) : SV_Target
{
    let N = normalize(input.normal);
    let L = normalize(-cb.lightDir.xyz);

    let NdotL   = saturate(dot(N, L));
    let diffuse = cb.lightColor.rgb * NdotL;
    let ambient = cb.ambientColor.rgb;

    let texColor  = texDiffuse.Sample(samLinear, input.uv);
    let vertColor = input.color;

    let geosetAlpha = cb.extraParams.x;
    let colorTint   = cb.extraParams.yzw;

    // Alpha test against texture alpha only (not affected by opacity fade)
    let texAlpha = texColor.a * vertColor.a;
    let alphaRef = cb.ambientColor.a;
    if (alphaRef > 0.0 && texAlpha < alphaRef)
        discard;

    let finalAlpha = texAlpha * geosetAlpha;
    if (geosetAlpha < 0.004)
        discard;

    // Unshaded: skip lighting, use full-bright texture
    let isUnshaded = cb.materialFlags.x > 0.5;
    let lighting = isUnshaded ? float3(1, 1, 1) : (ambient + diffuse);

    let finalColor = lighting * texColor.rgb * vertColor.rgb * colorTint;

    return float4(finalColor, finalAlpha);
}
