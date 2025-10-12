use bytemuck::cast_slice;
use glam::Vec3A;
use wgpu::util::DeviceExt;

use crate::camera::CAMERA_START;
use crate::rng::Pcg32;

pub const RANDOM_SPHERE_COUNT: usize = 1024;
const STATIC_SPHERE_COUNT: usize = RANDOM_SPHERE_COUNT + 1; // +1 for the ground sphere

pub struct SpheresGpu {
    pub count: u32,
    pub buf_center_radius: wgpu::Buffer,
    pub buf_albedo: wgpu::Buffer,
}

pub fn create_spheres(device: &wgpu::Device) -> SpheresGpu {
    let mut rng = Pcg32::new(0x9E37_79B9);
    let mut centers = Vec::with_capacity(STATIC_SPHERE_COUNT);
    let mut radii = Vec::with_capacity(STATIC_SPHERE_COUNT);
    let mut colors = Vec::with_capacity(STATIC_SPHERE_COUNT);

    centers.push(Vec3A::new(0.0, -1000.0, 0.0));
    radii.push(999.0);
    colors.push(Vec3A::splat(0.9));

    let mut attempts = 0usize;
    let max_attempts = RANDOM_SPHERE_COUNT * 32;
    while centers.len() < STATIC_SPHERE_COUNT && attempts < max_attempts {
        attempts += 1;

        let radius = 0.3 + rng.next_f32() * 0.7;
        let pos = Vec3A::new(
            (rng.next_f32() - 0.5) * 18.0,
            radius,
            (rng.next_f32() - 0.5) * 18.0 - 6.0,
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

    let mk = |label: &str, data: &[u8]| device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
        label: Some(label),
        contents: data,
        usage: wgpu::BufferUsages::STORAGE | wgpu::BufferUsages::COPY_DST,
    });

    SpheresGpu {
        count: centers.len() as u32,
        buf_center_radius: mk("s.center_radius", cast_slice(&center_radius)),
        buf_albedo: mk("s.albedo", cast_slice(&albedo)),
    }
}
