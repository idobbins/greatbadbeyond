use crate::gfx::Gfx;
use winit::{
    application::ApplicationHandler,
    event::WindowEvent,
    event_loop::{ActiveEventLoop, ControlFlow},
    window::WindowId,
};

pub struct App {
    gfx: Option<Gfx>,
}

impl App {
    pub fn new() -> Self {
        Self { gfx: None }
    }
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.gfx.is_none() {
            self.gfx = Some(Gfx::new(event_loop).expect("failed to init gfx"));
        }
        event_loop.set_control_flow(ControlFlow::Poll);
    }

    fn about_to_wait(&mut self, _event_loop: &ActiveEventLoop) {
        if let Some(gfx) = &self.gfx {
            gfx.window().request_redraw();
        }
    }

    fn window_event(
        &mut self,
        _event_loop: &ActiveEventLoop,
        _window_id: WindowId,
        event: WindowEvent,
    ) {
        use WindowEvent::*;

        let Some(gfx) = self.gfx.as_mut() else {
            return;
        };

        match event {
            CloseRequested => std::process::exit(0),
            Resized(size) => gfx.resize(size),
            RedrawRequested => {
                if let Err(e) = gfx.render() {
                    eprintln!("render error: {e}");
                }
            }
            _ => {}
        }
    }
}
