[[vk::binding(0, 0)]]
Texture2D textures[];
[[vk::binding(0, 0)]]
Texture2D<uint> texturesInt[];
[[vk::binding(0, 1)]]
SamplerState samplers[];

//struct UBOA
//{
//    float4x4 projection;
//    float4x4 view;
//    float time;
//    float2 resolution;
//};
//[[vk::binding(0, 0)]]
//ConstantBuffer<UBOA> uboa;

struct PushConsts
{
    uint tileMapIndex;
    uint tileSetStartIndex;
    float tileCountX;
    float tileCountY;
};
[[vk::push_constant]] PushConsts pushConsts;

struct UBO
{
    float4x4 mvp;
    float time;
    float timer;
    float tileMapSpeed;
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

[shader("vertex")]
VSOutput main(/*VSInput input, */ uint VertexIndex : SV_VertexID)
{
    VSOutput output;
    float2 uv = float2((VertexIndex << 1) & 2, VertexIndex & 2);
    output.uv = float2(1.0 - uv.x, 1.0 - uv.y);
    float2 offset = float2(ubo.mvp[0][3], ubo.mvp[1][3]);
    //offset = mul(ubo.view, mul(ubo.projection, float4(offset, 0.0, 1.0))).xy;
    output.uv.x += offset.x / ubo.tileMapSpeed;
    output.uv.y += offset.y / ubo.tileMapSpeed;
    output.pos = float4(uv * 2.0f + -1.0f, 0.0f, 1.0f);
    return output;
}

[shader("fragment")]
float4 main(VSOutput input) : SV_TARGET
{
    const float2 _div = float2(pushConsts.tileCountX, pushConsts.tileCountY);
    float2 uv = input.uv * _div;

    uint texIdx = texturesInt[pushConsts.tileMapIndex].Load(int3(uv, 0)) + pushConsts.tileSetStartIndex;

    uv.x = fmod(uv.x, 1.0);
    uv.y = fmod(uv.y, 1.0);

    float4 color = textures[texIdx].Sample(samplers[0], uv);
    return color;
}
