#version 460 core

layout(push_constant) uniform GradientParams
{
    vec2 resolution;
    float time;
    float padding;
} gradient;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main()
{
    vec2 safeResolution = max(gradient.resolution, vec2(1.0f));
    vec2 uv = clamp(fragUV / safeResolution, vec2(0.0f), vec2(1.0f));
    float wave = 0.5f + 0.5f*sin(gradient.time*0.5f);

    outColor = vec4(uv, wave, 1.0f);
}
