#version 450

layout(location = 0) out vec2 v_uv;

const vec2 POSITIONS[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2(3.0, -1.0),
    vec2(-1.0, 3.0)
);

const vec2 UVS[3] = vec2[](
    vec2(0.0, 0.0),
    vec2(2.0, 0.0),
    vec2(0.0, 2.0)
);

void main() {
    int idx = gl_VertexIndex;
    gl_Position = vec4(POSITIONS[idx], 0.0, 1.0);
    v_uv = UVS[idx];
}
