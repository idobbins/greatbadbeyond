use crate::gfx::{CameraInput, Gfx};
use std::{collections::HashSet, time::Instant};
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
}

#[derive(Default)]
struct InputState {
    pressed: HashSet<KeyCode>,
    mouse_captured: bool,
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
        }
    }

    fn set_cursor_capture(&mut self, capture: bool) {
        let Some(gfx) = self.gfx.as_mut() else {
            return;
        };

        if capture == self.input.mouse_captured {
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
        }
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
                if let Err(e) = gfx.render() {
                    eprintln!("render error: {e}");
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
                if button == MouseButton::Right{
                    self.set_cursor_capture(state == ElementState::Pressed);
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
