[[vk::binding(0, 0)]]
Texture2D textures[];
[[vk::binding(0, 0)]]
Texture2D<uint> texturesInt[];
[[vk::binding(0, 1)]]
SamplerState samplers[];

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
    float _unused0;
    float2 playerPos;
    float2 screenDim;
};
[[vk::binding(0, 2)]]
ConstantBuffer<UBO> ubo : register(b0, space2);

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
    output.uv = float2(uv.x - 0.5, uv.y - 0.5);
    output.uv += ubo.playerPos / (ubo.screenDim * 2.0f);
    output.pos = float4(uv * 2.0f + -1.0f, 0.0f, 1.0f);
    return output;
}

[shader("fragment")]
float4 main(VSOutput input) : SV_TARGET
{
    const float2 _div = float2(pushConsts.tileCountX, pushConsts.tileCountY);
    float2 uv = input.uv * _div;
    // Center at player
    uv += 0.5;
    uint texIdx = texturesInt[pushConsts.tileMapIndex].Load(int3(uv, 0)) + pushConsts.tileSetStartIndex;
    float2 locuv = uv;
    locuv.x = fmod(uv.x, 1.0);
    locuv.y = fmod(uv.y, 1.0);
    float4 color = textures[texIdx].Sample(samplers[0], locuv);
    return color;
}
