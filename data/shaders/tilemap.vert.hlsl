[[vk::binding(0, 0)]]
Texture2D textures[];

struct UBO
{
    float4x4 projection;
    float4x4 view;
    float time;
    float2 resolution;
};
[[vk::binding(0, 2)]]
ConstantBuffer<UBO> ubo : register(b0, space2);

//struct VSInput
//{
//    [[vk::location(0)]]float3 pos : POSITION0;
//    [[vk::location(1)]]float2 uv : TEXCOORD0;
//    // Instanced attributes
//    [[vk::location(2)]] float3 instancePos : POSITION1;
//    [[vk::location(3)]] float instanceScale: POSITION2;
//    [[vk::location(4)]] int instanceTextureIndex : TEXCOORD3;
//};

struct VSOutput
{
	float4 pos : SV_POSITION;
[[vk::location(0)]] float2 uv : TEXCOORD0;
[[vk::location(1)]] float4 color : COLOR0;
[[vk::location(2)]] nointerpolation int textureIndex : TEXCOORD1;
};

VSOutput main(/*VSInput input, */ uint VertexIndex : SV_VertexID)
{
    VSOutput output = (VSOutput) 0;
    float2 uv = float2((VertexIndex << 1) & 2, VertexIndex & 2);
    output.uv = float2(1.0 - uv.x, 1.0 - uv.y);
    output.uv.x += ubo.view[0][3] * 0.25;
    output.uv.y += ubo.view[1][3] * 0.25;
    output.pos = float4(uv * 2.0f + -1.0f, 0.0f, 1.0f);
    return output;
}
