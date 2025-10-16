#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0, rgba8) uniform writeonly image2D uTarget;

layout(push_constant) uniform PushConstants {
    uint width;
    uint height;
} pc;

void main()
{
    uvec2 id = gl_GlobalInvocationID.xy;
    if (id.x >= pc.width || id.y >= pc.height)
    {
        return;
    }

    vec2 size = vec2(max(pc.width - 1u, 1u), max(pc.height - 1u, 1u));
    vec2 uv = vec2(id) / size;
    vec3 color = vec3(uv, 0.5);

    imageStore(uTarget, ivec2(id), vec4(color, 1.0));
}
