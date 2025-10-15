use crate::gfx::{CameraInput, Gfx};
use std::{
    collections::{HashSet, VecDeque},
    time::{Duration, Instant},
};
use winit::{
    application::ApplicationHandler,
    event::{DeviceEvent, DeviceId, ElementState, MouseButton, WindowEvent},
    event_loop::{ActiveEventLoop, ControlFlow, DeviceEvents},
    keyboard::{KeyCode, PhysicalKey},
    window::{CursorGrabMode, WindowId},
};

pub struct App {
    gfx: Option<Gfx>,
    input: InputState,
    last_frame: Instant,
    frame_times: VecDeque<f32>,
    last_frame_log: Instant,
}

const FRAME_HISTORY: usize = 240;

#[derive(Default)]
struct InputState {
    pressed: HashSet<KeyCode>,
    mouse_captured: bool,
    look_left_held: bool,
    look_right_held: bool,
}

impl InputState {
    fn set_key(&mut self, key: KeyCode, pressed: bool) {
        if pressed {
            self.pressed.insert(key);
        } else {
            self.pressed.remove(&key);
        }
    }

    fn is_pressed(&self, key: KeyCode) -> bool {
        self.pressed.contains(&key)
    }
}

impl App {
    pub fn new() -> Self {
        Self {
            gfx: None,
            input: InputState::default(),
            last_frame: Instant::now(),
            frame_times: VecDeque::with_capacity(FRAME_HISTORY),
            last_frame_log: Instant::now(),
        }
    }

    fn set_cursor_capture(&mut self, capture: bool) {
        let Some(gfx) = self.gfx.as_mut() else {
            return;
        };

        if capture == self.input.mouse_captured {
            if !capture {
                self.input.look_left_held = false;
                self.input.look_right_held = false;
            }
            return;
        }

        let window = gfx.window();
        if capture {
            let mut result = window.set_cursor_grab(CursorGrabMode::Locked);
            if result.is_err() {
                result = window.set_cursor_grab(CursorGrabMode::Confined);
            }
            if let Err(err) = result {
                eprintln!("cursor grab failed: {err}");
                return;
            }
            window.set_cursor_visible(false);
            self.input.mouse_captured = true;
        } else {
            let _ = window.set_cursor_grab(CursorGrabMode::None);
            window.set_cursor_visible(true);
            self.input.mouse_captured = false;
            self.input.look_left_held = false;
            self.input.look_right_held = false;
        }
    }

    fn percentile(sorted: &[f32], pct: f32) -> f32 {
        debug_assert!(!sorted.is_empty());
        let clamped = pct.clamp(0.0, 100.0);
        let max_index = sorted.len().saturating_sub(1);
        if max_index == 0 {
            return sorted[0];
        }
        let idx = ((clamped / 100.0) * max_index as f32).round() as usize;
        sorted[idx.min(max_index)]
    }

    fn log_frame_stats(&self) {
        if self.frame_times.is_empty() {
            return;
        }

        let mut sorted: Vec<f32> = self.frame_times.iter().copied().collect();
        sorted.sort_by(|a, b| a.partial_cmp(b).unwrap());
        let p0 = Self::percentile(&sorted, 0.0);
        let p50 = Self::percentile(&sorted, 50.0);
        let p99 = Self::percentile(&sorted, 99.0);
        println!("frame time ms: p0={p0:.2} p50={p50:.2} p99={p99:.2}");
    }
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.gfx.is_none() {
            self.gfx = Some(Gfx::new(event_loop).expect("failed to init gfx"));
        }
        event_loop.set_control_flow(ControlFlow::Poll);
        event_loop.listen_device_events(DeviceEvents::Always);
        self.last_frame = Instant::now();
        self.last_frame_log = Instant::now();
        self.frame_times.clear();
    }

    fn about_to_wait(&mut self, _event_loop: &ActiveEventLoop) {
        let Some(gfx) = self.gfx.as_mut() else {
            return;
        };

        let now = Instant::now();
        let mut dt = (now - self.last_frame).as_secs_f32();
        self.last_frame = now;
        dt = dt.clamp(0.0, 0.1);

        let mut controls = CameraInput::default();
        if self.input.is_pressed(KeyCode::KeyW) {
            controls.forward += 1.0;
        }
        if self.input.is_pressed(KeyCode::KeyS) {
            controls.forward -= 1.0;
        }
        if self.input.is_pressed(KeyCode::KeyD) {
            controls.strafe += 1.0;
        }
        if self.input.is_pressed(KeyCode::KeyA) {
            controls.strafe -= 1.0;
        }
        if self.input.is_pressed(KeyCode::KeyE) {
            controls.ascend += 1.0;
        }
        if self.input.is_pressed(KeyCode::KeyQ) {
            controls.ascend -= 1.0;
        }
        controls.boost = self.input.is_pressed(KeyCode::ShiftLeft)
            || self.input.is_pressed(KeyCode::ShiftRight);

        gfx.update_camera(dt, controls);
        gfx.window().request_redraw();
    }

    fn window_event(
        &mut self,
        event_loop: &ActiveEventLoop,
        _window_id: WindowId,
        event: WindowEvent,
    ) {
        use WindowEvent::*;

        let Some(gfx) = self.gfx.as_mut() else {
            return;
        };

        match event {
            CloseRequested => {
                self.set_cursor_capture(false);
                event_loop.exit();
            }
            Resized(size) => gfx.resize(size),
            Focused(false) => self.set_cursor_capture(false),
            RedrawRequested => {
                let frame_start = Instant::now();
                if let Err(e) = gfx.render() {
                    eprintln!("render error: {e}");
                }
                let frame_time = frame_start.elapsed().as_secs_f32() * 1000.0;
                self.frame_times.push_back(frame_time);
                if self.frame_times.len() > FRAME_HISTORY {
                    self.frame_times.pop_front();
                }
                if self.last_frame_log.elapsed() >= Duration::from_secs(1) {
                    self.log_frame_stats();
                    self.last_frame_log = Instant::now();
                }
            }
            KeyboardInput { event, .. } => {
                if let PhysicalKey::Code(code) = event.physical_key {
                    let pressed = event.state == ElementState::Pressed;
                    self.input.set_key(code, pressed);
                    if pressed && matches!(code, KeyCode::Escape) {
                        self.set_cursor_capture(false);
                    }
                }
            }
            MouseInput { state, button, .. } => {
                match button {
                    MouseButton::Left => {
                        self.input.look_left_held = state == ElementState::Pressed;
                    }
                    MouseButton::Right => {
                        self.input.look_right_held = state == ElementState::Pressed;
                    }
                    _ => {}
                }
                if button == MouseButton::Left || button == MouseButton::Right {
                    let capture = self.input.look_left_held || self.input.look_right_held;
                    self.set_cursor_capture(capture);
                }
            }
            _ => {}
        }
    }

    fn device_event(
        &mut self,
        _event_loop: &ActiveEventLoop,
        _device_id: DeviceId,
        event: DeviceEvent,
    ) {
        if !self.input.mouse_captured {
            return;
        }
        if let DeviceEvent::MouseMotion { delta } = event {
            if let Some(gfx) = self.gfx.as_mut() {
                gfx.look_delta((delta.0 as f32, delta.1 as f32));
            }
        }
    }
}
