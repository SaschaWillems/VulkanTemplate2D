/*
 * Copyright (C) 2025 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

Texture2D fontTexture : register(t0);
SamplerState fontSampler : register(s0);

struct VSInput
{
	[[vk::location(0)]]float2 Pos : POSITION0;
	[[vk::location(1)]]float2 UV : TEXCOORD0;
	[[vk::location(2)]]float4 Color : COLOR0;
};

struct VSOutput
{
	float4 Pos : SV_POSITION;
	[[vk::location(0)]]float2 UV : TEXCOORD0;
	[[vk::location(1)]]float4 Color : COLOR0;
};

struct PushConstants
{
	float2 scale;
	float2 translate;
};

[[vk::push_constant]]
PushConstants pushConstants;

[shader("vertex")]
VSOutput main(VSInput input)
{
	VSOutput output;
	output.Pos = float4(input.Pos * pushConstants.scale + pushConstants.translate, 0.0, 1.0);
	output.UV = input.UV;
	output.Color = input.Color;
	return output;
}

[shader("fragment")]
float4 main(VSOutput input) : SV_TARGET
{
    return input.Color * fontTexture.Sample(fontSampler, input.UV);
}