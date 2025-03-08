/*
 * Copyright (C) 2025 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

struct UBO
{
    float4x4 projection;
    float4x4 view;
    float time;
    float timer;
};

cbuffer ubo : register(b0) { UBO ubo; }

struct VSOutput
{
	float4 Pos : SV_POSITION;
[[vk::location(0)]] float2 UV : TEXCOORD0;
};

[shader("vertex")]
VSOutput main(uint VertexIndex : SV_VertexID)
{
	VSOutput output = (VSOutput)0;
	output.UV = float2((VertexIndex << 1) & 2, VertexIndex & 2);
	output.Pos = float4(output.UV * 2.0f + -1.0f, 0.0f, 1.0f);
	return output;
}

float3 greyscale(float3 color, float str)
{
    float g = dot(color, float3(0.299, 0.587, 0.114));
    return lerp(color, float3(g.rrr), str);
}

[shader("fragment")]
float4 main(VSOutput input) : SV_TARGET
{
    float2 inUV = input.UV * 2.0 - 1.0;

    float a = ubo.timer * 2.0 * 3.14159265 * 0.5;
    float c = cos(a);
    float s = sin(a);
    float2x2 rm = float2x2(
        c, s,
        -s, c
    );
    inUV = mul(inUV, rm);

    float2 uv = inUV;
    uv = float2(atan2(uv.x, uv.y), ubo.time + 0.40 / length(uv));

    float4 color = sin(10.0 * uv.xyxy);

    return float4(greyscale(color.rgb, 1.0), 1.0);
}
