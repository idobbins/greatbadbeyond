#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba16f) readonly uniform image2D history_image;
layout(set = 0, binding = 1, rgba32f) readonly uniform image2D current_image;
layout(set = 0, binding = 2, rgba16f) writeonly uniform image2D accum_image;
layout(std140, set = 0, binding = 3) uniform AccumParams {
    uint reset_history;
    uint _pad0;
    uint _pad1;
    uint _pad2;
} u_accum;

const float SAMPLE_EPSILON = 0.5;
const float MAX_ACCUM_SAMPLES = 4096.0;
const float CLAMP_RELAXATION = 0.1;
const float MIN_RANGE = 1e-3;

void main() {
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    ivec2 dims = imageSize(accum_image);
    if (gid.x >= dims.x || gid.y >= dims.y) {
        return;
    }

    vec3 sample_color = imageLoad(current_image, gid).rgb;
    vec4 history = imageLoad(history_image, gid);

    float history_count = history.a;
    vec3 history_color = history.rgb;

    float new_count = 1.0;
    vec3 blended = sample_color;

    bool use_history = history_count > SAMPLE_EPSILON && u_accum.reset_history == 0u;

    if (use_history) {
        vec3 history_min = history_color;
        vec3 history_max = history_color;

        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                ivec2 coord = clamp(gid + ivec2(ox, oy), ivec2(0), dims - ivec2(1));
                vec4 neighbor = imageLoad(history_image, coord);
                if (neighbor.a > SAMPLE_EPSILON) {
                    history_min = min(history_min, neighbor.rgb);
                    history_max = max(history_max, neighbor.rgb);
                }
            }
        }

        vec3 range = max(history_max - history_min, vec3(MIN_RANGE));
        vec3 clamp_min = history_min - range * CLAMP_RELAXATION;
        vec3 clamp_max = history_max + range * CLAMP_RELAXATION;
        vec3 clamped_sample = clamp(sample_color, clamp_min, clamp_max);

        new_count = min(history_count + 1.0, MAX_ACCUM_SAMPLES);
        float blend_weight = 1.0 / new_count;
        blended = mix(history_color, clamped_sample, blend_weight);
    }

    imageStore(accum_image, gid, vec4(blended, new_count));
}
