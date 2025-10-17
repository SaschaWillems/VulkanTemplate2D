/*
 * Copyright (C) 2025 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

struct UBO
{
    float4x4 mvp;
    float time;
    float timer;
    float viewportAR;
    float postProcessTimer;
    float2 playerPos;
    float2 screenDim;
    float2 tilemapDim;
    uint32_t lightCount;
};
ConstantBuffer<UBO> ubo;

[[vk::binding(0, 1)]] Sampler2D inputImage;

struct LightSource {
    float2 pos;
    float3 color;
    float radius;
};
[[vk::binding(0, 2)]]
StructuredBuffer<LightSource> lights;

struct VSOutput
{
	float4 Pos : SV_POSITION;
[[vk::location(0)]] float2 UV;
};

float3 desaturate(float3 color, float factor)
{
    float3 lum = float3(0.299, 0.587, 0.114);
    float3 gray = float3(dot(lum, color));
    return lerp(color, gray, factor);
}

float3 fade(float3 color, float factor)
{
    return color * factor;
}

[shader("vertex")]
VSOutput main(uint VertexIndex : SV_VertexID)
{
	VSOutput output;
	output.UV = float2((VertexIndex << 1) & 2, VertexIndex & 2);
	output.Pos = float4(output.UV * 2.0f + -1.0f, 0.0f, 1.0f);
	return output;
}

[shader("fragment")]
float4 main(VSOutput input, uniform int effect)
{
    float3 texSample = inputImage.Sample(input.UV).rgb;

    float3 color = texSample.rgb * 0.03f;

    // Add light colors together
    for (uint32_t i = 0; i < ubo.lightCount; i++) {
        float2 l = input.UV - lights[i].pos;
        l.x *= ubo.viewportAR;
        float atten = min(length(l), lights[i].radius) / lights[i].radius;
        atten = pow(atten, 0.5);
        atten = 1.0f - atten;
        color += lights[i].color * texSample.rgb * atten;
    }

    //color = texSample.rgb;
    float3 finalColor = desaturate(color, 0.3);
    if (effect == 1) {
        finalColor = fade(finalColor, ubo.postProcessTimer);
    }

    return float4(finalColor, 1.0);
}