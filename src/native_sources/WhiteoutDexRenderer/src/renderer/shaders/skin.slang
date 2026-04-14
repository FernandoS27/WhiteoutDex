// ============================================================================
// WhiteoutDex Renderer — GPU vertex skinning (compute shader)
// Each thread skins one vertex: position (4x4) + normal (3x3), 4 bone influences.
// ============================================================================

struct Vertex
{
    float3 position;
    float3 normal;
    float4 color;
    float2 uv;
}

struct VertexWeight
{
    uint4  boneIdx;
    float4 weight;
}

// Node palette matrices are uploaded from C++ as row-major float[4][4].
// Declare row_major so the buffer data is interpreted correctly regardless
// of the global matrix layout setting.
StructuredBuffer<row_major float4x4> nodePalette;
StructuredBuffer<Vertex>             baseVerts;
StructuredBuffer<VertexWeight>       weights;

RWStructuredBuffer<Vertex>           outVerts;

static const int   kMaxBones  = 4;
static const float kMinWeight = 0.0001;

[shader("compute")]
[numthreads(256, 1, 1)]
void csSkin(uint3 dtid : SV_DispatchThreadID)
{
    let idx = dtid.x;

    uint vertCount, stride;
    baseVerts.GetDimensions(vertCount, stride);
    if (idx >= vertCount)
        return;

    let base = baseVerts[idx];
    let w    = weights[idx];

    float3 posSum = float3(0, 0, 0);
    float3 nrmSum = float3(0, 0, 0);
    float  totalW = 0;

    uint nodeCount, nodeStride;
    nodePalette.GetDimensions(nodeCount, nodeStride);

    uint  bones[kMaxBones] = { w.boneIdx.x, w.boneIdx.y, w.boneIdx.z, w.boneIdx.w };
    float wts[kMaxBones]   = { w.weight.x,  w.weight.y,  w.weight.z,  w.weight.w  };

    [ForceUnroll]
    for (int j = 0; j < kMaxBones; j++)
    {
        let bw = wts[j];
        if (bw < kMinWeight) continue;
        if (bones[j] >= nodeCount) continue;

        let mat = nodePalette[bones[j]];

        // Matrices are row-major in the buffer (matching C++ Matrix44f layout).
        // Use row-vector convention: v * M.
        posSum += bw * mul(float4(base.position, 1.0), mat).xyz;
        nrmSum += bw * mul(base.normal, (float3x3)mat);
        totalW += bw;
    }

    Vertex result;
    if (totalW < kMinWeight)
    {
        result = base;
    }
    else
    {
        result.position = posSum;
        result.normal   = normalize(nrmSum);
        result.color    = base.color;
        result.uv       = base.uv;
    }

    outVerts[idx] = result;
}
