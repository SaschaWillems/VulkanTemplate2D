[[vk::binding(0, 0)]]
Texture2D textures[];
[[vk::binding(0, 1)]]
SamplerState samplers[];

struct VSInput
{
    [[vk::location(0)]] float3 Pos : POSITION0;
    [[vk::location(1)]] float2 UV : TEXCOORD0;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

[shader("vertex")]
VSOutput main(VSInput input, uint VertexIndex: SV_VertexID)
{
    VSOutput output;
    output.Pos = float4(input.Pos, 1.0f);
    output.UV = input.UV;
    return output;
}

[shader("fragment")]
float4 main(VSOutput input, uniform uint textureIndex)
{
    return textures[textureIndex].Sample(samplers[0], input.UV);
}
