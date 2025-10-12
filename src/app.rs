use std::sync::Arc;

use winit::{
    application::ApplicationHandler,
    event::{DeviceEvent, WindowEvent},
    event_loop::{ActiveEventLoop, EventLoop},
    keyboard::{KeyCode, PhysicalKey},
    window::{CursorGrabMode, Window},
};

use crate::input::InputState;
use crate::state::State;

pub fn run() {
    let event_loop = EventLoop::new().expect("event loop");
    let mut app = App::default();
    event_loop.run_app(&mut app).expect("run app");
}

#[derive(Default)]
struct App {
    state: Option<State>,
    input: InputState,
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        let window = Arc::new(
            event_loop
                .create_window(Window::default_attributes().with_title("callandor - spheres"))
                .expect("create window"),
        );
        self.state = Some(pollster::block_on(State::new(window)));
    }

    fn window_event(
        &mut self,
        event_loop: &ActiveEventLoop,
        _id: winit::window::WindowId,
        event: WindowEvent,
    ) {
        let Some(state) = self.state.as_mut() else {
            return;
        };
        match event {
            WindowEvent::CloseRequested => event_loop.exit(),
            WindowEvent::Resized(size) => state.resize(size.width, size.height),
            WindowEvent::RedrawRequested => state.frame(&mut self.input),
            WindowEvent::MouseInput {
                state: winit::event::ElementState::Pressed,
                ..
            } => {
                if !self.input.mouse_locked {
                    let _ = state
                        .window()
                        .set_cursor_grab(CursorGrabMode::Confined)
                        .or_else(|_| state.window().set_cursor_grab(CursorGrabMode::Locked));
                    state.window().set_cursor_visible(false);
                    self.input.mouse_locked = true;
                }
            }
            WindowEvent::KeyboardInput { event, .. } => {
                if let PhysicalKey::Code(code) = event.physical_key {
                    if code == KeyCode::Escape && event.state.is_pressed() {
                        let _ = state.window().set_cursor_grab(CursorGrabMode::None);
                        state.window().set_cursor_visible(true);
                        self.input.mouse_locked = false;
                    } else {
                        self.input.set_key(code, event.state.is_pressed());
                    }
                }
            }
            _ => {}
        }
    }

    fn device_event(
        &mut self,
        _event_loop: &ActiveEventLoop,
        _device_id: winit::event::DeviceId,
        event: DeviceEvent,
    ) {
        if let DeviceEvent::MouseMotion { delta } = event {
            if self.input.mouse_locked {
                self.input.mouse_dx += delta.0 as f32;
                self.input.mouse_dy += delta.1 as f32;
            }
        }
    }
}
