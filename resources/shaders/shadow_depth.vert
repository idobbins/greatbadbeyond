#version 460 core

layout(push_constant) uniform ShadowPushConstants
{
    mat4 mvp;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inInstanceTranslation;

void main()
{
    gl_Position = pc.mvp*vec4(inPosition + inInstanceTranslation.xyz, 1.0);
}
