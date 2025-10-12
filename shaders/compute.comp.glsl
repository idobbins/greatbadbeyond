#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba8) writeonly uniform image2D out_image;

void main() {
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    ivec2 dims = imageSize(out_image);
    if (gid.x >= dims.x || gid.y >= dims.y) {
        return;
    }
    vec2 uv = vec2(gid) / vec2(dims);
    imageStore(out_image, gid, vec4(uv, 0.0, 1.0));
}
