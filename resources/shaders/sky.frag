#version 460 core

layout(std140, set = 0, binding = 0) uniform FrameGlobals
{
    mat4 viewProj;
    vec4 cameraPosition;
    vec4 sunDirection;
    uvec4 lightGrid;
    vec4 frameParams;
} fg;

layout(location = 0) in vec3 fragViewDir;
layout(location = 0) out vec4 outColor;

vec3 EvaluateSky(vec3 viewDir, vec3 sunDir)
{
    float t = clamp(viewDir.y*0.5 + 0.5, 0.0, 1.0);
    vec3 horizon = vec3(0.42, 0.52, 0.66);
    vec3 zenith = vec3(0.06, 0.13, 0.24);
    vec3 sky = mix(horizon, zenith, t);
    float sunAmount = max(dot(viewDir, sunDir), 0.0);
    sky += vec3(1.0, 0.88, 0.70) * pow(sunAmount, 192.0) * 4.0;
    sky += vec3(1.0, 0.93, 0.82) * pow(sunAmount, 48.0) * 0.18;
    return sky;
}

void main()
{
    vec3 sunDir = normalize(fg.sunDirection.xyz);
    vec3 skyColor = EvaluateSky(normalize(fragViewDir), sunDir);
    outColor = vec4(skyColor, 1.0);
}
