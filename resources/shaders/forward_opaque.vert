#version 460 core

layout(push_constant) uniform ForwardPushConstants
{
    mat4 mvp;
    vec4 tint;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUV;

void main()
{
    gl_Position = pc.mvp*vec4(inPosition, 1.0);
    fragNormal = inNormal;
    fragUV = inUV;
}
