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

// Blend factor for applying tent filtered result over the original
#ifndef POST_AA_STRENGTH
#define POST_AA_STRENGTH 0.35
#endif

// Eight by eight Bayer matrix mapped to [-0.5, +0.5]
float bayer8x8(ivec2 p)
{
    const int M[64] = int[64](
         0, 32,  8, 40,  2, 34, 10, 42,
        48, 16, 56, 24, 50, 18, 58, 26,
        12, 44,  4, 36, 14, 46,  6, 38,
        60, 28, 52, 20, 62, 30, 54, 22,
         3, 35, 11, 43,  1, 33,  9, 41,
        51, 19, 59, 27, 49, 17, 57, 25,
        15, 47,  7, 39, 13, 45,  5, 37,
        63, 31, 55, 23, 61, 29, 53, 21
    );
    int index = (p.y & 7) * 8 + (p.x & 7);
    return (float(M[index]) / 63.0) - 0.5;
}

void main()
{
    vec4 c = texture(uGradient, vUV);

    // Apply 3x3 tent weights to reduce residual moire shimmer
    ivec2 textureSize0 = textureSize(uGradient, 0);
    vec2 texel = 1.0 / vec2(max(textureSize0, ivec2(1)));

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

    vec4 filtered = mix(c, tent, POST_AA_STRENGTH);

    // The compute stage outputs gamma corrected color
    vec3 rgb = filtered.rgb;

    // Apply ordered dithering with amplitude close to one UNORM LSB
    float dither = bayer8x8(ivec2(gl_FragCoord.xy));
    const float amplitude = 1.0 / 255.0;
    rgb += dither * amplitude;

    outColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
#endif
