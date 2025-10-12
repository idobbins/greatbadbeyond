use bytemuck::{Pod, Zeroable, cast_slice};
use glam::{UVec3, Vec3A};
use wgpu::util::DeviceExt;

use crate::camera::CAMERA_START;
use crate::rng::Pcg32;

pub const RANDOM_SPHERE_COUNT: usize = 10_000;
const STATIC_SPHERE_COUNT: usize = RANDOM_SPHERE_COUNT + 1; // +1 for the ground sphere
const GROUND_IDX: usize = 0;

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable, Default, Debug)]
pub struct GridParams {
    pub bmin: [f32; 3],
    _pad0: u32,
    pub bmax: [f32; 3],
    _pad1: u32,
    pub dims: [u32; 3],
    _pad2: u32,
    pub inv_cell: [f32; 3],
    _pad3: u32,
}

pub struct SpheresGpu {
    pub count: u32,
    pub buf_center_radius: wgpu::Buffer,
    pub buf_albedo: wgpu::Buffer,

    pub buf_grid_params: wgpu::Buffer,
    pub buf_l0_cells: wgpu::Buffer,
    pub buf_l1_cells: wgpu::Buffer,
    pub buf_cell_indices: wgpu::Buffer,
}

pub fn create_spheres(device: &wgpu::Device) -> SpheresGpu {
    let mut rng = Pcg32::new(0x9E37_79B9);
    let mut centers = Vec::with_capacity(STATIC_SPHERE_COUNT);
    let mut radii = Vec::with_capacity(STATIC_SPHERE_COUNT);
    let mut colors = Vec::with_capacity(STATIC_SPHERE_COUNT);

    centers.push(Vec3A::new(0.0, -1000.0, 0.0));
    radii.push(999.0);
    colors.push(Vec3A::splat(0.9));

    let spawn_extent = (RANDOM_SPHERE_COUNT as f32).sqrt() * 1.2;

    let mut attempts = 0usize;
    let max_attempts = RANDOM_SPHERE_COUNT * 64;
    while centers.len() < STATIC_SPHERE_COUNT && attempts < max_attempts {
        attempts += 1;

        let radius = 0.3 + rng.next_f32() * 0.7;
        let pos = Vec3A::new(
            (rng.next_f32() - 0.5) * 2.0 * spawn_extent,
            radius,
            (rng.next_f32() - 0.5) * 2.0 * spawn_extent - 6.0,
        );

        if pos.distance(CAMERA_START) < radius + 1.0 {
            continue;
        }

        let mut intersects = false;
        for (other_center, other_radius) in centers.iter().zip(radii.iter()) {
            let min_dist = other_radius + radius + 0.1;
            if pos.distance(*other_center) < min_dist {
                intersects = true;
                break;
            }
        }
        if intersects {
            continue;
        }

        let hue = Vec3A::new(rng.next_f32(), rng.next_f32(), rng.next_f32());
        let albedo = hue.clamp(Vec3A::splat(0.2), Vec3A::splat(0.95));

        centers.push(pos);
        radii.push(radius);
        colors.push(albedo);
    }

    if centers.len() < STATIC_SPHERE_COUNT {
        eprintln!(
            "warning: only spawned {} / {} spheres due to overlap constraints",
            centers.len(),
            STATIC_SPHERE_COUNT
        );
    }

    let mut center_radius = Vec::with_capacity(centers.len());
    let mut albedo = Vec::with_capacity(colors.len());
    for ((center, radius), color) in centers.iter().zip(radii.iter()).zip(colors.iter()) {
        center_radius.push([center.x, center.y, center.z, *radius]);
        albedo.push([color.x, color.y, color.z, 0.0]);
    }

    #[derive(Clone, Copy)]
    struct Aabb {
        min: Vec3A,
        max: Vec3A,
    }

    let mut scene_aabb = Aabb {
        min: Vec3A::splat(f32::INFINITY),
        max: Vec3A::splat(f32::NEG_INFINITY),
    };
    for (i, (c, r)) in centers.iter().zip(radii.iter()).enumerate() {
        if i == GROUND_IDX {
            continue;
        }
        let r3 = Vec3A::splat(*r);
        scene_aabb.min = scene_aabb.min.min(*c - r3);
        scene_aabb.max = scene_aabb.max.max(*c + r3);
    }

    if centers.len() <= 1 {
        scene_aabb.min = Vec3A::splat(-1.0);
        scene_aabb.max = Vec3A::splat(1.0);
    }

    let margin = 0.5;
    scene_aabb.min -= Vec3A::splat(margin);
    scene_aabb.max += Vec3A::splat(margin);

    let mut ext = scene_aabb.max - scene_aabb.min;
    ext = Vec3A::new(ext.x.max(0.1), ext.y.max(0.1), ext.z.max(0.1));

    let n_non_ground = (centers.len().saturating_sub(1)).max(1) as f32;
    let total_cells_target = (n_non_ground / 4.0).max(1.0);
    let base = total_cells_target.cbrt();
    let maxe = ext.max_element();
    let dims_f = [
        (ext.x / maxe * base).max(1.0),
        (ext.y / maxe * base).max(1.0),
        (ext.z / maxe * base).max(1.0),
    ];
    let dims = UVec3::new(
        dims_f[0].round().clamp(1.0, 64.0) as u32,
        dims_f[1].round().clamp(1.0, 64.0) as u32,
        dims_f[2].round().clamp(1.0, 64.0) as u32,
    );
    let cell_size = Vec3A::new(
        ext.x / dims.x.max(1) as f32,
        ext.y / dims.y.max(1) as f32,
        ext.z / dims.z.max(1) as f32,
    );
    let inv_cell = Vec3A::new(
        if cell_size.x.abs() > f32::MIN_POSITIVE {
            1.0 / cell_size.x
        } else {
            0.0
        },
        if cell_size.y.abs() > f32::MIN_POSITIVE {
            1.0 / cell_size.y
        } else {
            0.0
        },
        if cell_size.z.abs() > f32::MIN_POSITIVE {
            1.0 / cell_size.z
        } else {
            0.0
        },
    );

    let l0_len = (dims.x * dims.y * dims.z).max(1) as usize;
    let mut l0_lists: Vec<Vec<u32>> = vec![Vec::new(); l0_len];

    let clamp_uvw = |p: Vec3A| -> UVec3 {
        let rel = (p - scene_aabb.min) * inv_cell;
        UVec3::new(
            rel.x.floor().clamp(0.0, (dims.x - 1) as f32) as u32,
            rel.y.floor().clamp(0.0, (dims.y - 1) as f32) as u32,
            rel.z.floor().clamp(0.0, (dims.z - 1) as f32) as u32,
        )
    };
    let flatten = |x: u32, y: u32, z: u32| -> usize { (x + dims.x * (y + dims.y * z)) as usize };

    for i in 1..centers.len() {
        let c = centers[i];
        let r = radii[i];
        let min = c - Vec3A::splat(r);
        let max = c + Vec3A::splat(r);
        let mn = clamp_uvw(min);
        let mx = clamp_uvw(max);
        for z in mn.z..=mx.z {
            for y in mn.y..=mx.y {
                for x in mn.x..=mx.x {
                    l0_lists[flatten(x, y, z)].push(i as u32);
                }
            }
        }
    }

    const MAX_LEAF: usize = 16;
    const CHILD_DIM: u32 = 4;
    let child_total = (CHILD_DIM * CHILD_DIM * CHILD_DIM) as usize;

    let mut l0_headers: Vec<[u32; 4]> = Vec::with_capacity(l0_len);
    let mut l1_headers: Vec<[u32; 2]> = Vec::new();
    let mut indices: Vec<u32> = Vec::new();

    for idx in 0..l0_len {
        let list = &mut l0_lists[idx];
        if list.len() <= MAX_LEAF {
            let start = indices.len() as u32;
            indices.extend_from_slice(list.as_slice());
            l0_headers.push([start, list.len() as u32, u32::MAX, 0]);
        } else {
            let child_base = l1_headers.len() as u32;
            let mut ch_lists: Vec<Vec<u32>> = vec![Vec::new(); child_total];

            let z = idx as u32 / (dims.x * dims.y);
            let rem = idx as u32 - z * dims.x * dims.y;
            let y = rem / dims.x;
            let x = rem % dims.x;

            let cell_min = scene_aabb.min + Vec3A::new(x as f32, y as f32, z as f32) * cell_size;
            let child_inv = inv_cell * CHILD_DIM as f32;

            let clamp_child = |p: Vec3A| -> UVec3 {
                let rel = (p - cell_min) * child_inv;
                UVec3::new(
                    rel.x.floor().clamp(0.0, (CHILD_DIM - 1) as f32) as u32,
                    rel.y.floor().clamp(0.0, (CHILD_DIM - 1) as f32) as u32,
                    rel.z.floor().clamp(0.0, (CHILD_DIM - 1) as f32) as u32,
                )
            };
            let ch_flat = |cx: u32, cy: u32, cz: u32| -> usize {
                (cx + CHILD_DIM * (cy + CHILD_DIM * cz)) as usize
            };

            for &sid in list.iter() {
                let c = centers[sid as usize];
                let r = radii[sid as usize];
                let mn = clamp_child(c - Vec3A::splat(r));
                let mx = clamp_child(c + Vec3A::splat(r));
                for cz in mn.z..=mx.z {
                    for cy in mn.y..=mx.y {
                        for cx in mn.x..=mx.x {
                            ch_lists[ch_flat(cx, cy, cz)].push(sid);
                        }
                    }
                }
            }

            for ch in ch_lists.into_iter() {
                let start = indices.len() as u32;
                let count = ch.len() as u32;
                indices.extend_from_slice(ch.as_slice());
                l1_headers.push([start, count]);
            }

            l0_headers.push([0, 0, child_base, 1]);
        }
    }

    if l1_headers.is_empty() {
        l1_headers.push([0, 0]);
    }
    if indices.is_empty() {
        indices.push(0);
    }

    let mk = |label: &str, data: &[u8], usage: wgpu::BufferUsages| {
        device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some(label),
            contents: data,
            usage,
        })
    };

    let params = GridParams {
        bmin: scene_aabb.min.to_array(),
        _pad0: 0,
        bmax: scene_aabb.max.to_array(),
        _pad1: 0,
        dims: [dims.x, dims.y, dims.z],
        _pad2: 0,
        inv_cell: inv_cell.to_array(),
        _pad3: 0,
    };

    SpheresGpu {
        count: centers.len() as u32,
        buf_center_radius: mk(
            "s.center_radius",
            cast_slice(&center_radius),
            wgpu::BufferUsages::STORAGE | wgpu::BufferUsages::COPY_DST,
        ),
        buf_albedo: mk(
            "s.albedo",
            cast_slice(&albedo),
            wgpu::BufferUsages::STORAGE | wgpu::BufferUsages::COPY_DST,
        ),
        buf_grid_params: mk(
            "grid.params",
            cast_slice(&[params]),
            wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
        ),
        buf_l0_cells: mk(
            "grid.l0",
            cast_slice(&l0_headers),
            wgpu::BufferUsages::STORAGE | wgpu::BufferUsages::COPY_DST,
        ),
        buf_l1_cells: mk(
            "grid.l1",
            cast_slice(&l1_headers),
            wgpu::BufferUsages::STORAGE | wgpu::BufferUsages::COPY_DST,
        ),
        buf_cell_indices: mk(
            "grid.indices",
            cast_slice(&indices),
            wgpu::BufferUsages::STORAGE | wgpu::BufferUsages::COPY_DST,
        ),
    }
}
