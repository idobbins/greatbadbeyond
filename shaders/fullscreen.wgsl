@group(0) @binding(0) var src_tex: texture_2d<f32>;
@group(0) @binding(1) var src_smp: sampler;

struct VsOut {
  @builtin(position) pos: vec4<f32>,
  @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vi: u32) -> VsOut {
  var positions = array<vec2<f32>, 3>(
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0),
  );
  var uvs = array<vec2<f32>, 3>(
    vec2(0.0, 0.0),
    vec2(2.0, 0.0),
    vec2(0.0, 2.0),
  );
  var out: VsOut;
  out.pos = vec4(positions[vi], 0.0, 1.0);
  out.uv = uvs[vi];
  return out;
}

@fragment
fn fs_main(input: VsOut) -> @location(0) vec4<f32> {
  // Flip Y so the storage image's top-left origin displays correctly.
  let uv = vec2<f32>(input.uv.x, 1.0 - input.uv.y);
  return textureSample(src_tex, src_smp, uv);
}
