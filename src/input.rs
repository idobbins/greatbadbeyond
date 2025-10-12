use winit::keyboard::KeyCode;

#[derive(Default)]
pub struct InputState {
    pub w: bool,
    pub a: bool,
    pub s: bool,
    pub d: bool,
    pub q: bool,
    pub e: bool,
    pub shift: bool,
    pub ctrl: bool,
    pub mouse_locked: bool,
    pub mouse_dx: f32,
    pub mouse_dy: f32,
}

impl InputState {
    pub fn set_key(&mut self, code: KeyCode, pressed: bool) {
        match code {
            KeyCode::KeyW => self.w = pressed,
            KeyCode::KeyA => self.a = pressed,
            KeyCode::KeyS => self.s = pressed,
            KeyCode::KeyD => self.d = pressed,
            KeyCode::KeyQ => self.q = pressed,
            KeyCode::KeyE => self.e = pressed,
            KeyCode::ShiftLeft | KeyCode::ShiftRight => self.shift = pressed,
            KeyCode::ControlLeft | KeyCode::ControlRight => self.ctrl = pressed,
            _ => {}
        }
    }

    pub fn consume_mouse_delta(&mut self) -> (f32, f32) {
        let delta = (self.mouse_dx, self.mouse_dy);
        self.mouse_dx = 0.0;
        self.mouse_dy = 0.0;
        delta
    }
}
