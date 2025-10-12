use glam::Vec3A;

pub const CAMERA_START: Vec3A = Vec3A::from_array([0.0, 1.0, 4.0]);

#[derive(Clone, Copy)]
pub struct Camera {
    pub pos: Vec3A,
    pub yaw: f32,
    pub pitch: f32,
    pub fovy_deg: f32,
}

impl Camera {
    pub fn new() -> Self {
        Self {
            pos: CAMERA_START,
            yaw: std::f32::consts::PI,
            pitch: 0.0,
            fovy_deg: 60.0,
        }
    }

    pub fn basis(&self) -> (Vec3A, Vec3A, Vec3A) {
        let (sy, cy) = self.yaw.sin_cos();
        let (sp, cp) = self.pitch.sin_cos();
        let forward = Vec3A::new(cy * cp, sp, -sy * cp).normalize();
        let right = forward.cross(Vec3A::Y).normalize_or_zero();
        let up = right.cross(forward).normalize_or_zero();
        (forward, right, up)
    }
}
