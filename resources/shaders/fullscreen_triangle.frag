#version 460 core

layout(push_constant) uniform GradientParams
{
    vec2 resolution;
    float time;
    float padding;
} gradient;

layout(set = 0, binding = 1) uniform sampler2D pathTracerImage;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main()
{
    vec2 safeResolution = max(gradient.resolution, vec2(1.0f));
    vec2 uv = clamp(fragUV / safeResolution, vec2(0.0f), vec2(1.0f));
    outColor = texture(pathTracerImage, uv);
}
