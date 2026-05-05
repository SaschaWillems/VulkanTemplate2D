[[vk::binding(0, 0)]]
Texture2D textures[];
[[vk::binding(0, 1)]]
SamplerState samplers[];

struct UBO
{
    float4x4 mvp;
	float time;
	float2 resolution;
};
[[vk::binding(0, 2)]]
ConstantBuffer<UBO> ubo : register(b0,space2);

struct UBOA
{
    float4x4 projection;
    float4x4 view;
    float time;
    float2 resolution;
};
[[vk::binding(0, 0)]]
ConstantBuffer<UBOA> uboa;

struct VSInput
{
    [[vk::location(0)]] float3 pos : POSITION0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    // Instanced attributes
    [[vk::location(2)]] int2 instancePos : POSITION1;
    [[vk::location(3)]] int instanceTextureIndex : TEXCOORD3;
};

struct VSOutput
{
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] nointerpolation int textureIndex : TEXCOORD1;
};

[shader("vertex")]
VSOutput main(VSInput input)
{
    VSOutput output;
    float3 locPos = input.pos + float3(input.instancePos.x, input.instancePos.y, 0.0);
    output.pos = mul(ubo.mvp, float4(locPos, 1.0));
    output.uv = input.uv;
    output.textureIndex = input.instanceTextureIndex;
    output.color = float4(1.0);
    return output;
}

[shader("fragment")]
float4 main(VSOutput input) : SV_TARGET
{
    float4 color = textures[input.textureIndex].Sample(samplers[0], input.uv);
    return color * input.color;
}
