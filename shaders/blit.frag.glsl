#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler tex_sampler;
layout(set = 0, binding = 1) uniform texture2D tex;

vec3 tonemap(vec3 hdr) {
    return hdr / (hdr + 1.0);
}

void main() {
    vec3 hdr = texture(sampler2D(tex, tex_sampler), v_uv).rgb;
    vec3 ldr = tonemap(max(hdr, vec3(0.0)));
    out_color = vec4(ldr, 1.0);
}
