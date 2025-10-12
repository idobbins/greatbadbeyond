#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler tex_sampler;
layout(set = 0, binding = 1) uniform texture2D tex;

void main() {
    out_color = texture(sampler2D(tex, tex_sampler), v_uv);
}
