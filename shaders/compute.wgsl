// GPU-built uniform grid + 3D-DDA traversal + spheres touching plane.
// References: Amanatides & Woo DDA; uniform grid on GPU; two-level grids (see notes at end).

struct Params {
  cam_pos: vec3<f32>,       sphere_count: u32,
  cam_fwd: vec3<f32>,       _pad0: u32,
  cam_right: vec3<f32>,     _pad1: u32,
  cam_up: vec3<f32>,        _pad2: u32,
  half_fov_tan: f32,        aspect: f32,
  ground_y: f32,            ambient: f32,
  light_dir: vec3<f32>,     _pad3: u32,
  light_color: vec3<f32>,   _pad4: u32,

  // --- uniform grid params (isotropic cell size) ---
  grid_origin: vec3<f32>,   grid_cell_size: f32,
  grid_dims: vec3<u32>,     grid_max_per_cell: u32,
};

// --- Materials ---
const MAT_DIFFUSE: f32 = 0.0;
const MAT_METAL:   f32 = 1.0;
const MAT_GLASS:   f32 = 2.0;
const IOR_GLASS:   f32 = 1.5;

@group(0) @binding(0)
var out_img: texture_storage_2d<rgba8unorm, write>;

@group(0) @binding(1)
var<storage, read_write> spheres: array<vec4<f32>>;   // (cx,cy,cz,r)

@group(0) @binding(2)
var<storage, read_write> albedos: array<vec4<f32>>;   // (r,g,b,_)

@group(0) @binding(3)
var<uniform> params: Params;

// Per-cell counts (atomic), and flat list of primitive indices.
// grid_list length = product(grid_dims) * grid_max_per_cell
@group(0) @binding(4)
var<storage, read_write> grid_counts: array<atomic<u32>>;
@group(0) @binding(5)
var<storage, read_write> grid_list: array<u32>;

fn hash1(x: f32) -> f32 {
  return fract(sin(x * 12.9898) * 43758.5453);
}

fn reflect_vec(i: vec3<f32>, n: vec3<f32>) -> vec3<f32> {
  return i - 2.0 * dot(n, i) * n;
}

fn fresnel_schlick(cosTheta: f32, F0: f32) -> f32 {
  let inv = 1.0 - cosTheta;
  return F0 + (1.0 - F0) * inv * inv * inv * inv * inv;
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

// ----------------- Grid helpers -----------------
fn grid_bounds_min() -> vec3<f32> { return params.grid_origin; }
fn grid_bounds_max() -> vec3<f32> {
  let dims = vec3<f32>(f32(params.grid_dims.x), f32(params.grid_dims.y), f32(params.grid_dims.z));
  return params.grid_origin + dims * params.grid_cell_size;
}

fn world_to_cell(p: vec3<f32>) -> vec3<i32> {
  return vec3<i32>(floor((p - params.grid_origin) / params.grid_cell_size));
}

fn clamp_cell(c: vec3<i32>) -> vec3<i32> {
  let maxc = vec3<i32>(
    i32(params.grid_dims.x) - 1, i32(params.grid_dims.y) - 1, i32(params.grid_dims.z) - 1
  );
  return clamp(c, vec3<i32>(0,0,0), maxc);
}

fn cell_index(ci: vec3<i32>) -> u32 {
  let d = params.grid_dims;
  return u32(ci.z) * d.x * d.y + u32(ci.y) * d.x + u32(ci.x);
}

fn ray_box(ro: vec3<f32>, rd: vec3<f32>, bmin: vec3<f32>, bmax: vec3<f32>) -> vec2<f32> {
  let inv = vec3<f32>(1.0/rd.x, 1.0/rd.y, 1.0/rd.z);
  let t0 = (bmin - ro) * inv;
  let t1 = (bmax - ro) * inv;
  let tsm = min(t0, t1);
  let tsM = max(t0, t1);
  let tmin = max(max(tsm.x, tsm.y), tsm.z);
  let tmax = min(min(tsM.x, tsM.y), tsM.z);
  return vec2<f32>(tmin, tmax);
}

struct Hit {
  t: f32,
  idx: u32,
};

fn traverse_grid_first_hit(ro: vec3<f32>, rd: vec3<f32>, t_stop: f32, skip_idx: u32) -> Hit {
  let bmin = grid_bounds_min();
  let bmax = grid_bounds_max();
  let box_t = ray_box(ro, rd, bmin, bmax);
  var t_enter = max(box_t.x, 0.0);
  let t_exit  = box_t.y;
  if (t_exit <= t_enter) { return Hit(1e30, 0xffffffffu); }

  // Initial cell and stepping params
  let pos0 = ro + rd * (t_enter + 1e-5); // push in to avoid boundary edge cases
  var ci = clamp_cell(world_to_cell(pos0));

  let step = vec3<i32>(
    select(-1, 1, rd.x > 0.0),
    select(-1, 1, rd.y > 0.0),
    select(-1, 1, rd.z > 0.0)
  );

  // Boundaries of current cell
  let cell_min = params.grid_origin + vec3<f32>(f32(ci.x), f32(ci.y), f32(ci.z)) * params.grid_cell_size;
  let cell_max = cell_min + vec3<f32>(params.grid_cell_size);

  // t to next boundary on each axis
  let next_x = select(cell_min.x, cell_max.x, step.x > 0);
  let next_y = select(cell_min.y, cell_max.y, step.y > 0);
  let next_z = select(cell_min.z, cell_max.z, step.z > 0);

  let inv_rd = vec3<f32>(1.0/rd.x, 1.0/rd.y, 1.0/rd.z);
  var tMax = vec3<f32>(
    t_enter + (next_x - pos0.x) * inv_rd.x,
    t_enter + (next_y - pos0.y) * inv_rd.y,
    t_enter + (next_z - pos0.z) * inv_rd.z
  );
  let tDelta = vec3<f32>(
    abs(params.grid_cell_size * inv_rd.x),
    abs(params.grid_cell_size * inv_rd.y),
    abs(params.grid_cell_size * inv_rd.z)
  );

  var best_t = 1e30;
  var best_idx: u32 = 0xffffffffu;
  var t = t_enter;
  let t_limit = min(t_exit, t_stop);

  loop {
    if (t > t_limit) { break; }
    // visit cell
    let cidx = cell_index(ci);
    let cnt = min(atomicLoad(&grid_counts[cidx]), params.grid_max_per_cell);
    let base = cidx * params.grid_max_per_cell;

    let t_cell_end = min(tMax.x, min(tMax.y, tMax.z));
    for (var j: u32 = 0u; j < cnt; j = j + 1u) {
      let s_idx = grid_list[base + j];
      if (s_idx == skip_idx) { continue; }
      let s = spheres[s_idx];
      let hit_t = intersect_sphere(ro, rd, s.xyz, s.w);
      if (hit_t < best_t && hit_t <= t_cell_end) {
        best_t = hit_t;
        best_idx = s_idx;
        // early out: the first hit inside this cell is closest overall
      }
    }
    if (best_idx != 0xffffffffu) { break; }

    // step DDA
    if (tMax.x <= tMax.y && tMax.x <= tMax.z) {
      ci.x += step.x;
      t = tMax.x;
      tMax.x += tDelta.x;
      if (ci.x < 0 || ci.x >= i32(params.grid_dims.x)) { break; }
    } else if (tMax.y <= tMax.x && tMax.y <= tMax.z) {
      ci.y += step.y;
      t = tMax.y;
      tMax.y += tDelta.y;
      if (ci.y < 0 || ci.y >= i32(params.grid_dims.y)) { break; }
    } else {
      ci.z += step.z;
      t = tMax.z;
      tMax.z += tDelta.z;
      if (ci.z < 0 || ci.z >= i32(params.grid_dims.z)) { break; }
    }
  }

  return Hit(best_t, best_idx);
}

fn shade_at(p: vec3<f32>, n: vec3<f32>, base: vec3<f32>, self_idx: u32) -> vec3<f32> {
  let L = normalize(-params.light_dir);
  let ro = p + n * 1e-3;
  let hit = traverse_grid_first_hit(ro, L, 1e30, self_idx);
  let blocked = (hit.idx != 0xffffffffu);
  let ndotl = max(dot(n, L), 0.0);
  let shadow = select(0.0, 1.0, blocked);
  return base * params.ambient + base * params.light_color * ndotl * (1.0 - shadow);
}

// Matte-only trace (used for specular/refraction bounce). Ignores secondary materials.
fn trace_matte(ro: vec3<f32>, rd: vec3<f32>, skip_idx: u32) -> vec3<f32> {
  let t_plane = intersect_plane_y(ro, rd, params.ground_y);
  let hit = traverse_grid_first_hit(ro, rd, t_plane, skip_idx);
  if (hit.idx != 0xffffffffu) {
    let s = spheres[hit.idx];
    let p_hit = ro + rd * hit.t;
    let n = normalize(p_hit - s.xyz);
    let base = albedos[hit.idx].xyz;
    return shade_at(p_hit, n, base, hit.idx);
  }
  if (t_plane < 1e30) {
    let p_hit = ro + rd * t_plane;
    let n = vec3<f32>(0.0, 1.0, 0.0);
    let scale = 0.5;
    let checker = (i32(floor(p_hit.x * scale) + floor(p_hit.z * scale)) & 1);
    let base = select(vec3<f32>(0.92, 0.92, 0.92), vec3<f32>(0.15, 0.15, 0.15), checker != 0);
    return shade_at(p_hit, n, base, 0xffffffffu);
  }
  let t = 0.5 * (rd.y + 1.0);
  let c0 = vec3<f32>(0.60, 0.75, 1.00);
  let c1 = vec3<f32>(0.05, 0.10, 0.20);
  return c0 + t * (c1 - c0);
}

// --- Entry #0: clear per-cell counts ---
@compute @workgroup_size(256, 1, 1)
fn grid_clear_main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let total = params.grid_dims.x * params.grid_dims.y * params.grid_dims.z;
  if (gid.x < total) {
    atomicStore(&grid_counts[gid.x], 0u);
  }
}

// --- Entry #1: generate SoA sphere data on GPU (non-overlapping grid) ---
@compute @workgroup_size(64, 1, 1)
fn gen_main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let i = gid.x;
  if (i >= params.sphere_count) { return; }

  let gn = u32(ceil(sqrt(f32(params.sphere_count))));
  let gx = i % gn;
  let gz = i / gn;

  let max_r = 0.5;
  let min_r = 0.3;
  let r = min_r + hash1(f32(i) * 3.17) * (max_r - min_r);

  let spacing = 2.2 * max_r;
  let fx = (f32(gx) - 0.5 * f32(gn - 1u)) * spacing;
  let fz = (f32(gz) - 0.5 * f32(gn - 1u)) * spacing;

  // Touch the ground plane exactly (center at y = ground_y + r)
  let cy = params.ground_y + r;

  spheres[i] = vec4<f32>(fx, cy, fz, r);

  // Material selection (15% metal, 10% glass, rest diffuse)
  let msel = fract(hash1(f32(i) * 5.67) + hash1(f32(i) * 7.89 + 3.14));
  var mtype: f32 = MAT_DIFFUSE;
  if (msel < 0.15) {
    mtype = MAT_METAL;
  } else if (msel < 0.25) {
    mtype = MAT_GLASS;
  }

  var c = vec3<f32>(0.0, 0.0, 0.0);
  if (mtype == MAT_METAL) {
    c = vec3<f32>(
      0.6 + 0.4 * hash1(f32(i) * 1.23),
      0.6 + 0.4 * hash1(f32(i) * 2.34),
      0.6 + 0.4 * hash1(f32(i) * 4.56)
    );
  } else if (mtype == MAT_GLASS) {
    c = vec3<f32>(
      0.15 + 0.3 * hash1(f32(i) * 1.23),
      0.15 + 0.3 * hash1(f32(i) * 2.34),
      0.30 + 0.4 * hash1(f32(i) * 4.56)
    );
  } else {
    c = vec3<f32>(
      0.2 + 0.8 * hash1(f32(i) * 1.23),
      0.2 + 0.8 * hash1(f32(i) * 2.34),
      0.2 + 0.8 * hash1(f32(i) * 4.56)
    );
  }
  albedos[i] = vec4<f32>(c, mtype);
}

// --- Entry #2: build uniform grid (one pass, atomics) ---
@compute @workgroup_size(64, 1, 1)
fn grid_build_main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let i = gid.x;
  if (i >= params.sphere_count) { return; }

  let s = spheres[i];
  let c = s.xyz; let r = s.w;
  let cmin = clamp_cell(world_to_cell(c - vec3<f32>(r)));
  let cmax = clamp_cell(world_to_cell(c + vec3<f32>(r)));

  let xmin = u32(cmin.x); let xmax = u32(cmax.x);
  let ymin = u32(cmin.y); let ymax = u32(cmax.y);
  let zmin = u32(cmin.z); let zmax = u32(cmax.z);

  for (var z: u32 = zmin; z <= zmax; z = z + 1u) {
    for (var y: u32 = ymin; y <= ymax; y = y + 1u) {
      for (var x: u32 = xmin; x <= xmax; x = x + 1u) {
        let cid = u32(z) * params.grid_dims.x * params.grid_dims.y + u32(y) * params.grid_dims.x + u32(x);
        let slot = atomicAdd(&grid_counts[cid], 1u);
        if (slot < params.grid_max_per_cell) {
          grid_list[cid * params.grid_max_per_cell + slot] = i;
        }
      }
    }
  }
}

// --- Entry #3: trace the scene via grid traversal ---
@compute @workgroup_size(16, 16, 1)
fn trace_main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let dims = textureDimensions(out_img);
  if (any(gid.xy >= dims)) { return; }

  let uv01 = (vec2<f32>(f32(gid.x) + 0.5, f32(gid.y) + 0.5)) / vec2<f32>(f32(dims.x), f32(dims.y));
  var p = uv01 * 2.0 - vec2<f32>(1.0, 1.0);
  p.x = p.x * params.aspect;

  let rd = normalize(params.cam_fwd + p.x * params.half_fov_tan * params.cam_right - p.y * params.half_fov_tan * params.cam_up);
  let ro = params.cam_pos;

  let t_plane = intersect_plane_y(ro, rd, params.ground_y);

  // Sphere hit with DDA up to the plane (if any)
  let hit = traverse_grid_first_hit(ro, rd, t_plane, 0xffffffffu);

  var color = vec3<f32>(0.0, 0.0, 0.0);
  if (hit.idx != 0xffffffffu) {
    let s = spheres[hit.idx];
    let p_hit = ro + rd * hit.t;
    let n = normalize(p_hit - s.xyz);
    let base = albedos[hit.idx].xyz;
    let m = albedos[hit.idx].w;

    if (m == MAT_DIFFUSE) {
      color = shade_at(p_hit, n, base, hit.idx);
    } else if (m == MAT_METAL) {
      let R = normalize(reflect_vec(rd, n));
      let rc = trace_matte(p_hit + R * 1e-3, R, hit.idx);
      color = base * rc;
    } else {
      var N = n;
      var etai = 1.0;
      var etat = IOR_GLASS;
      var cosi = dot(rd, N);
      if (cosi > 0.0) {
        N = -N;
        let tmp = etai;
        etai = etat;
        etat = tmp;
        cosi = dot(rd, N);
      }
      let cosTheta = -cosi;
      let eta = etai / etat;
      let F0 = ((etat - etai) / (etat + etai));
      let Rf = fresnel_schlick(cosTheta, F0 * F0);

      let Rdir = normalize(reflect_vec(rd, N));
      let Rcol = trace_matte(p_hit + Rdir * 1e-3, Rdir, hit.idx);

      var Tcol = vec3<f32>(0.0, 0.0, 0.0);
      let k = 1.0 - eta * eta * (1.0 - cosTheta * cosTheta);
      if (k > 0.0) {
        let Tdir = normalize(eta * rd + (eta * cosTheta - sqrt(k)) * N);
        Tcol = trace_matte(p_hit + Tdir * 1e-3, Tdir, hit.idx);
      }
      color = Rf * Rcol + (1.0 - Rf) * (base * Tcol);
    }
  } else if (t_plane < 1e30) {
    let p_hit = ro + rd * t_plane;
    let n = vec3<f32>(0.0, 1.0, 0.0);
    let scale = 0.5;
    let checker = (i32(floor(p_hit.x * scale) + floor(p_hit.z * scale)) & 1);
    let base = select(vec3<f32>(0.92, 0.92, 0.92), vec3<f32>(0.15, 0.15, 0.15), checker != 0);
    color = shade_at(p_hit, n, base, 0xffffffffu);
  } else {
    let t = 0.5 * (rd.y + 1.0);
    let c0 = vec3<f32>(0.60, 0.75, 1.00);
    let c1 = vec3<f32>(0.05, 0.10, 0.20);
    color = c0 + t * (c1 - c0);
  }

  textureStore(out_img, vec2<i32>(gid.xy), vec4<f32>(color, 1.0));
}
