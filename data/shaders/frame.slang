[[vk::binding(0, 0)]]
Texture2D textures[];
[[vk::binding(0, 1)]]
SamplerState samplers[];

struct VSOutput
{
    float4 Pos : SV_POSITION;
    [[vk::location(0)]] float2 UV;
};

[shader("vertex")]
VSOutput main(uint VertexIndex: SV_VertexID)
{
    VSOutput output;
    output.UV = float2((VertexIndex << 1) & 2, VertexIndex & 2);
    output.Pos = float4(output.UV * 2.0f + -1.0f, 0.0f, 1.0f);
    return output;
}

[shader("fragment")]
float4 main(VSOutput input, uniform uint textureIndex) : SV_TARGET
{
    return textures[textureIndex].Sample(samplers[0], input.UV);
}
