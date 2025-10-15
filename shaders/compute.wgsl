// Deterministic fixed-time ray tracer with SoA sphere buffers + ground plane

struct Params {
  cam_pos: vec3<f32>,       sphere_count: u32,
  cam_fwd: vec3<f32>,       _pad0: u32,
  cam_right: vec3<f32>,     _pad1: u32,
  cam_up: vec3<f32>,        _pad2: u32,
  half_fov_tan: f32,        aspect: f32,
  ground_y: f32,            ambient: f32,
  light_dir: vec3<f32>,     _pad3: u32,
  light_color: vec3<f32>,   _pad4: u32,
};

@group(0) @binding(0)
var out_img: texture_storage_2d<rgba8unorm, write>;

// SoA: positions+radius in one buffer, albedo in another buffer
// spheres[i] = (cx, cy, cz, r)
@group(0) @binding(1)
var<storage, read_write> spheres: array<vec4<f32>>;
// albedos[i] = (r, g, b, _)
@group(0) @binding(2)
var<storage, read_write> albedos: array<vec4<f32>>;

@group(0) @binding(3)
var<uniform> params: Params;

fn hash1(x: f32) -> f32 {
  // simple deterministic hash to [0,1)
  return fract(sin(x * 12.9898) * 43758.5453);
}

fn intersect_sphere(ro: vec3<f32>, rd: vec3<f32>, c: vec3<f32>, r: f32) -> f32 {
  let oc = ro - c;
  let b = dot(oc, rd);
  let c0 = dot(oc, oc) - r*r;
  let disc = b*b - c0;
  if (disc <= 0.0) { return 1e30; }
  let s = sqrt(disc);
  let t0 = -b - s;
  let t1 = -b + s;
  if (t0 > 0.0) { return t0; }
  if (t1 > 0.0) { return t1; }
  return 1e30;
}

fn intersect_plane_y(ro: vec3<f32>, rd: vec3<f32>, y0: f32) -> f32 {
  let denom = rd.y;
  if (abs(denom) < 1e-5) { return 1e30; }
  let t = (y0 - ro.y) / denom;
  return select(1e30, t, t > 0.0);
}

fn shade_at(p: vec3<f32>, n: vec3<f32>, base: vec3<f32>) -> vec3<f32> {
  let L = normalize(-params.light_dir);
  // hard shadow (fixed-time: iterate all spheres)
  let ro = p + n * 1e-3;
  var blocked = false;
  for (var i: u32 = 0u; i < params.sphere_count; i = i + 1u) {
    let s = spheres[i];
    let t = intersect_sphere(ro, L, s.xyz, s.w);
    blocked = blocked || (t < 1e30);
  }
  let ndotl = max(dot(n, L), 0.0);
  let shadow = select(0.0, 1.0, blocked);
  // ambient + unshadowed lambert
  return base * params.ambient + base * params.light_color * ndotl * (1.0 - shadow);
}

// --- Entry #1: generate SoA sphere data on GPU (non-overlapping grid) ---
@compute @workgroup_size(64, 1, 1)
fn gen_main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let i = gid.x;
  if (i >= params.sphere_count) { return; }

  // Grid dims covering all spheres
  let gn = u32(ceil(sqrt(f32(params.sphere_count))));
  let gx = i % gn;
  let gz = i / gn;

  let max_r = 0.5;
  let min_r = 0.3;
  let r = min_r + hash1(f32(i) * 3.17) * (max_r - min_r);

  // spacing > 2*max_r guarantees no overlap
  let spacing = 2.2 * max_r;
  let fx = (f32(gx) - 0.5 * f32(gn - 1u)) * spacing;
  let fz = (f32(gz) - 0.5 * f32(gn - 1u)) * spacing;
  let cy = params.ground_y + 1.5 * r; // center is 1.5R above plane => sphere bottom at 0.5R above plane

  spheres[i] = vec4<f32>(fx, cy, fz, r);

  // deterministic albedo
  let c = vec3<f32>(
    0.2 + 0.8 * hash1(f32(i) * 1.23),
    0.2 + 0.8 * hash1(f32(i) * 2.34),
    0.2 + 0.8 * hash1(f32(i) * 4.56)
  );
  albedos[i] = vec4<f32>(c, 1.0);
}

// --- Entry #2: trace the scene into the storage image ---
@compute @workgroup_size(16, 16, 1)
fn trace_main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let dims = textureDimensions(out_img);
  if (any(gid.xy >= dims)) { return; }

  // Screen space in [-1,1], aspect handled explicitly.
  let uv01 = (vec2<f32>(f32(gid.x) + 0.5, f32(gid.y) + 0.5)) / vec2<f32>(f32(dims.x), f32(dims.y));
  var p = uv01 * 2.0 - vec2<f32>(1.0, 1.0);
  p.x = p.x * params.aspect;

  let rd = normalize(params.cam_fwd + p.x * params.half_fov_tan * params.cam_right - p.y * params.half_fov_tan * params.cam_up);
  let ro = params.cam_pos;

  // Intersect spheres
  var t_hit = 1e30;
  var hit_idx: u32 = 0u;
  var hit_kind: u32 = 0u; // 0: none, 1: sphere, 2: plane

  for (var i: u32 = 0u; i < params.sphere_count; i = i + 1u) {
    let s = spheres[i];
    let t = intersect_sphere(ro, rd, s.xyz, s.w);
    if (t < t_hit) {
      t_hit = t;
      hit_idx = i;
      hit_kind = 1u;
    }
  }

  // Intersect ground plane y = ground_y
  let t_plane = intersect_plane_y(ro, rd, params.ground_y);
  if (t_plane < t_hit) {
    t_hit = t_plane;
    hit_kind = 2u;
  }

  var color = vec3<f32>(0.0, 0.0, 0.0);

  if (hit_kind == 0u) {
    // Sky gradient
    let t = 0.5 * (rd.y + 1.0);
    let c0 = vec3<f32>(0.60, 0.75, 1.00);
    let c1 = vec3<f32>(0.05, 0.10, 0.20);
    color = c0 + t * (c1 - c0);
  } else if (hit_kind == 1u) {
    let s = spheres[hit_idx];
    let p_hit = ro + rd * t_hit;
    let n = normalize(p_hit - s.xyz);
    let base = albedos[hit_idx].xyz;
    color = shade_at(p_hit, n, base);
  } else { // plane
    let p_hit = ro + rd * t_hit;
    let n = vec3<f32>(0.0, 1.0, 0.0);
    // simple large checker to help read shadows
    let scale = 0.5;
    let checker = (i32(floor(p_hit.x * scale) + floor(p_hit.z * scale)) & 1);
    let base = select(vec3<f32>(0.92, 0.92, 0.92), vec3<f32>(0.15, 0.15, 0.15), checker != 0);
    color = shade_at(p_hit, n, base);
  }

  textureStore(out_img, vec2<i32>(gid.xy), vec4<f32>(color, 1.0));
}
