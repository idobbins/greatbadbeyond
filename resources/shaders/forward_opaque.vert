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
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inInstanceTranslation;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragWorldPos;

void main()
{
    vec4 modelPos = pc.model*vec4(inPosition, 1.0);
    vec3 worldPos = modelPos.xyz + inInstanceTranslation.xyz;
    gl_Position = fg.viewProj*vec4(worldPos, 1.0);
    fragNormal = normalize(mat3(pc.model)*inNormal);
    fragUV = inUV;
    fragWorldPos = worldPos;
}
