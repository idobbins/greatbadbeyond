#version 460

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(binding = 1) uniform sampler2D accumulator_sampler;

void main() {
  const vec2 uv = clamp(v_uv, 0.0, 1.0);
  const vec3 sample_color = texture(accumulator_sampler, uv).rgb;
  out_color = vec4(sample_color, 1.0);
}
