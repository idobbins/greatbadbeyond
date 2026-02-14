#version 460 core

layout(push_constant) uniform ForwardPushConstants
{
    mat4 model;
    vec4 tint;
} pc;

layout(std140, set = 0, binding = 0) uniform FrameGlobals
{
    mat4 viewProj;
    vec4 cameraPosition;
    vec4 sunDirection;
    uvec4 lightGrid;
    vec4 frameParams;
} fg;

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 fragViewDir;

void main()
{
    vec3 worldPos = fg.cameraPosition.xyz + inPosition*120.0;
    gl_Position = fg.viewProj*vec4(worldPos, 1.0);
    fragViewDir = normalize(worldPos - fg.cameraPosition.xyz);
}
