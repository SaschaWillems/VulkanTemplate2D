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
};

cbuffer ubo { UBO ubo; }

[[vk::binding(0, 1)]] Sampler2D inputImage;

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

[shader("vertex")]
VSOutput main(uint VertexIndex : SV_VertexID)
{
	VSOutput output = (VSOutput)0;
	output.UV = float2((VertexIndex << 1) & 2, VertexIndex & 2);
	output.Pos = float4(output.UV * 2.0f + -1.0f, 0.0f, 1.0f);
	return output;
}

[shader("fragment")]
float4 main(VSOutput input)
{
    float3 inputColor = inputImage.Sample(input.UV).rgb;
    inputColor = desaturate(inputColor, 0.3);
    return float4(inputColor, 1.0);
}