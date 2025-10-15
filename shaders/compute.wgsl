@group(0) @binding(0)
var out_img: texture_storage_2d<rgba8unorm, write>;

@compute @workgroup_size(16, 16, 1)
fn cs_main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let dims = textureDimensions(out_img);
  if (any(gid.xy >= dims)) {
    return;
  }

  let uv = (vec2(f32(gid.x) + 0.5, f32(gid.y) + 0.5)) / vec2(f32(dims.x), f32(dims.y));
  textureStore(out_img, vec2<i32>(gid.xy), vec4<f32>(uv, 0.0, 1.0));
}
