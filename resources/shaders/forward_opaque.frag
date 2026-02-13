#version 460 core

layout(push_constant) uniform ForwardPushConstants
{
    mat4 mvp;
    vec4 tint;
} pc;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main()
{
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(vec3(0.5, 1.0, 0.35));
    float lambert = max(dot(N, L), 0.15);

    vec3 base = vec3(fragUV, 1.0 - fragUV.y);
    vec3 lit = base*lambert*pc.tint.rgb;
    outColor = vec4(lit, pc.tint.a);
}
