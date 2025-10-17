#version 450

#include "bindings.inc.glsl"

#ifdef VERTEX_SHADER
layout(location = 0) out vec2 vUV;

void main()
{
    const vec2 positions[3] = vec2[3](
        vec2(-1.0, -3.0),
        vec2(-1.0, 1.0),
        vec2(3.0, 1.0)
    );

    const vec2 uvs[3] = vec2[3](
        vec2(0.0, 2.0),
        vec2(0.0, 0.0),
        vec2(2.0, 0.0)
    );

    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    vUV = uvs[gl_VertexIndex];
}
#endif

#ifdef FRAGMENT_SHADER
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(binding = B_SAMPLER) uniform sampler2D uGradient;

void main()
{
    outColor = texture(uGradient, vUV);
}
#endif
