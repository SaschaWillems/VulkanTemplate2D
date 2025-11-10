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
    float2 screenRes;
    float2 tilemapDim;
    uint32_t lightCount;
    float dayNightCycle;
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

// From https://babylonjs.medium.com/retro-crt-shader-a-post-processing-effect-study-1cb3f783afbc
float2 curveRemapUV(float2 uv)
{
    const float2 curvature = float2(3.5);
    uv = uv * 2.0 - 1.0;
    float2 offset = abs(uv.yx) / float2(curvature.x, curvature.y);
    uv = uv + uv * offset * offset;
    uv = uv * 0.5 + 0.5;
    return uv;
}

// From https://babylonjs.medium.com/retro-crt-shader-a-post-processing-effect-study-1cb3f783afbc
float4 scanLineIntensity(float uv, float resolution, float opacity)
{
    float intensity = sin(uv * resolution * float.getPi() * 2.0);
    intensity = ((0.5 * intensity) + 0.5) * 0.9 + 0.1;
    return float4(float3(pow(intensity, opacity)), 1.0);
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
    // CRT curvature
    float2 uv = curveRemapUV(input.UV);

    float3 texSample = inputImage.Sample(uv).rgb;
    float3 color = texSample.rgb * ubo.dayNightCycle;

    // Add light colors together
    for (uint32_t i = 0; i < ubo.lightCount; i++) {
        float2 l = uv - lights[i].pos;
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

    // CRT scanlines
    finalColor *= scanLineIntensity(uv.x, ubo.screenRes.y, 0.5).xyz;
    finalColor *= scanLineIntensity(uv.y, ubo.screenRes.x, 0.5).xyz;

    return float4(finalColor, 1.0);
}