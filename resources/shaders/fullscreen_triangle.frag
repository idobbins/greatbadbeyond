#version 460 core

layout(push_constant) uniform SolidColor
{
    vec4 tint;
} solidColor;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = solidColor.tint;
}
