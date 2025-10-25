#version 460 core

layout(push_constant) uniform GradientParams
{
    vec2 resolution;
    float time;
    float padding;
} gradient;

layout(location = 0) out vec2 fragUV;

vec2 positions[3] = vec2[](
    vec2(-1.0, -3.0),
    vec2(-1.0, 1.0),
    vec2(3.0, 1.0)
);

void main()
{
    vec2 position = positions[gl_VertexIndex];
    gl_Position = vec4(position, 0.0, 1.0);

    vec2 normalized = vec2(
        position.x*0.5f + 0.5f,
        -position.y*0.5f + 0.5f
    );

    fragUV = normalized*gradient.resolution;
}
