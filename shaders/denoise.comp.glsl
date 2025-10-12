#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba32f) readonly uniform image2D in_image;
layout(set = 0, binding = 1, rgba8) writeonly uniform image2D out_image;

const float SIGMA_S = 1.5;
const float SIGMA_R = 0.25;
const float INV_SIGMA_S = 1.0 / (2.0 * SIGMA_S * SIGMA_S);
const float INV_SIGMA_R = 1.0 / (2.0 * SIGMA_R * SIGMA_R);
const float BLEND_WEIGHT = 0.7;

vec3 tonemap(vec3 c) {
    return c / (c + 1.0);
}

void main() {
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    ivec2 dims = imageSize(out_image);
    if (gid.x >= dims.x || gid.y >= dims.y) {
        return;
    }

    vec3 center = imageLoad(in_image, gid).rgb;
    vec3 center_tm = tonemap(center);

    float weight_sum = 0.0;
    vec3 accum = vec3(0.0);

    for (int oy = -2; oy <= 2; ++oy) {
        for (int ox = -2; ox <= 2; ++ox) {
            ivec2 coord = clamp(gid + ivec2(ox, oy), ivec2(0), dims - ivec2(1));
            vec3 neighbor = imageLoad(in_image, coord).rgb;
            vec3 neighbor_tm = tonemap(neighbor);

            vec2 offset = vec2(ox, oy);
            float spatial = exp(-dot(offset, offset) * INV_SIGMA_S);
            vec3 diff = neighbor_tm - center_tm;
            float range = exp(-dot(diff, diff) * INV_SIGMA_R);
            float weight = max(spatial * range, 1e-4);

            accum += neighbor * weight;
            weight_sum += weight;
        }
    }

    if (weight_sum <= 0.0) {
        accum = center;
        weight_sum = 1.0;
    }

    vec3 filtered = accum / weight_sum;
    vec3 filtered_tm = tonemap(filtered);
    vec3 final_tm = mix(center_tm, filtered_tm, BLEND_WEIGHT);

    imageStore(out_image, gid, vec4(final_tm, 1.0));
}
