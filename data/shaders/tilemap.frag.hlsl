[[vk::binding(0, 0)]]
Texture2D textures[];
[[vk::binding(0, 0)]]
Texture2D<uint> texturesInt[];
[[vk::binding(0, 1)]]
SamplerState samplers[];

struct UBO
{
	float4x4 projection;
	float4x4 view;
	float time;
	float2 resolution;
};
[[vk::binding(0, 0)]]
ConstantBuffer<UBO> ubo;

struct PushConsts
{
    uint tileMapIndex;
    uint tileSetStartIndex;
    float tileCountX;
    float tileCountY;
};
[[vk::push_constant]] PushConsts pushConsts;


struct VSOutput
{
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] nointerpolation int textureIndex : TEXCOORD1;
};

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