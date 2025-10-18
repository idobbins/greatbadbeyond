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

// How much to blend the blurred image over the original.
// Tweak between 0.20–0.45 to taste.
#ifndef POST_AA_STRENGTH
#define POST_AA_STRENGTH 0.35
#endif

void main()
{
    vec4 c = texture(uGradient, vUV);

    // 3x3 tent filter: (1 2 1; 2 4 2; 1 2 1) / 16
    // Cheap, stable, and good at killing moiré.
    ivec2 sz = textureSize(uGradient, 0);
    vec2 texel = 1.0 / vec2(max(sz, ivec2(1)));

    vec2 dx = vec2(texel.x, 0.0);
    vec2 dy = vec2(0.0, texel.y);

    vec4 s0 = texture(uGradient, vUV - dx - dy);
    vec4 s1 = texture(uGradient, vUV - dy);
    vec4 s2 = texture(uGradient, vUV + dx - dy);
    vec4 s3 = texture(uGradient, vUV - dx);
    vec4 s4 = c;
    vec4 s5 = texture(uGradient, vUV + dx);
    vec4 s6 = texture(uGradient, vUV - dx + dy);
    vec4 s7 = texture(uGradient, vUV + dy);
    vec4 s8 = texture(uGradient, vUV + dx + dy);

    vec4 tent = (s4 * 4.0 +
                 (s1 + s3 + s5 + s7) * 2.0 +
                 (s0 + s2 + s6 + s8)) * (1.0 / 16.0);

    outColor = mix(c, tent, POST_AA_STRENGTH);
}
#endif
