struct UBO
{
	float4x4 projection;
	float4x4 view;
	float time;
	float2 resolution;
};
[[vk::binding(0, 2)]]
ConstantBuffer<UBO> ubo : register(b0,space2);

struct InstanceData
{
    float4 instancePosScale;
    uint instanceTextureIndex;
    uint instanceEffect;   
};
[[vk::binding(1, 2)]]
StructuredBuffer<InstanceData> instances;

static float3 vertices[6] =
{    
    float3(1.0, 1.0, 0.0),
    float3(-1.0, 1.0, 0.0),
    float3(-1.0, -1.0, 0.0),
    float3(-1.0, -1.0, 0.0),
    float3(1.0, -1.0, 0.0),
    float3(1.0, 1.0, 0.0),
};

static float2 uvs[6] =
{
    float2(1.0, 1.0),
    float2(0.0, 1.0),
    float2(0.0, 0.0),
    float2(0.0, 0.0),
    float2(1.0, 0.0),
    float2(1.0, 1.0),
};

struct VSOutput
{
	float4 pos : SV_POSITION;
[[vk::location(0)]] float2 uv : TEXCOORD0;
[[vk::location(1)]] float4 color : COLOR0;
[[vk::location(2)]] nointerpolation int textureIndex : TEXCOORD1;
};

struct VSInput
{
    [[vk::location(0)]]float3 pos : POSITION0;
    [[vk::location(1)]]float2 uv : TEXCOORD0;
};

VSOutput main(VSInput input, uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    VSOutput output = (VSOutput) 0;
    float3 locPos = float3((vertices[vertexIndex] * instances[instanceIndex].instancePosScale.w) + instances[instanceIndex].instancePosScale.xyz);
    output.pos = mul(ubo.view, mul(ubo.projection, float4(locPos, 1.0)));
    output.uv = uvs[vertexIndex];
    output.textureIndex = instances[instanceIndex].instanceTextureIndex;
    if (instances[instanceIndex].instanceEffect == 1)
    {
        output.color = float4(20.0, 20.0, 20.0, 1.0);
    }
    else
    {
        output.color = float4((1.0).rrrr);
    }
    return output;
}
