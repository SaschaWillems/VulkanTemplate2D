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
    [[vk::location(2)]] int2 instancePos;
    [[vk::location(3)]] int instanceTextureIndex;
    [[vk::location(4)]] int instanceEffect;
};

struct VSOutput
{
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float2 uv;
    [[vk::location(1)]] float4 color;
    [[vk::location(2)]] nointerpolation int textureIndex;
    [[vk::location(3)]] nointerpolation int instanceEffect;
};

[shader("vertex")]
VSOutput main(VSInput input, uniform int2 selectedTile)
{
    VSOutput output;
    float3 locPos = input.pos + float3(input.instancePos.x, input.instancePos.y, 0.0);
    output.pos = mul(ubo.mvp, float4(locPos, 1.0));
    output.uv = input.uv;
    output.textureIndex = input.instanceTextureIndex;
    output.color = float4(1.0);
    output.instanceEffect = input.instanceEffect;
    if ((input.instancePos.x == selectedTile.x) && (input.instancePos.y == selectedTile.y)) {
        output.color *= 16.0;
    }
    return output;
}

[shader("fragment")]
float4 main(VSOutput input) : SV_TARGET
{
    float4 color = textures[input.textureIndex].Sample(samplers[0], input.uv);
    if (input.instanceEffect == 1) {
        if (input.uv.x < 0.05 || input.uv.x > 0.95 || input.uv.y < 0.05 || input.uv.y > 0.95) {
            input.color = 100.0;
        }
    } else if (input.instanceEffect == 2) {
        if (input.uv.x < 0.05 || input.uv.x > 0.95 || input.uv.y < 0.05 || input.uv.y > 0.95) {
            input.color = 0.0;
        }
    }
    return color * input.color;
}
