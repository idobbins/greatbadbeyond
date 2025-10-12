use std::sync::Arc;
use std::time::Instant;

use bytemuck::{Pod, Zeroable};
use glam::Vec3A;
use wgpu::{util::make_spirv, util::DeviceExt, ShaderStages, TextureFormat};
use winit::{
    application::ApplicationHandler,
    event::{DeviceEvent, WindowEvent},
    event_loop::{ActiveEventLoop, EventLoop},
    keyboard::{KeyCode, PhysicalKey},
    window::{CursorGrabMode, Window},
};

const COMPUTE_SPV: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/compute.comp.glsl.spv"));
const BLIT_VERT_SPV: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/blit.vert.glsl.spv"));
const BLIT_FRAG_SPV: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/blit.frag.glsl.spv"));

const RANDOM_SPHERE_COUNT: usize = 64;
const STATIC_SPHERE_COUNT: usize = RANDOM_SPHERE_COUNT + 1; // +1 for the ground sphere
const CAMERA_START: Vec3A = Vec3A::from_array([0.0, 1.0, 4.0]);

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable, Debug)]
struct CameraUbo {
    cam_pos: [f32; 3],
    _pad0: f32,
    cam_fwd: [f32; 3],
    _pad1: f32,
    cam_right: [f32; 3],
    _pad2: f32,
    cam_up: [f32; 3],
    _pad3: f32,
    tan_half_fovy: f32,
    aspect: f32,
    frame_index: u32,
    sphere_count: u32,
}

struct SpheresGpu {
    count: u32,
    buf_cx: wgpu::Buffer,
    buf_cy: wgpu::Buffer,
    buf_cz: wgpu::Buffer,
    buf_r: wgpu::Buffer,
    buf_cr: wgpu::Buffer,
    buf_cg: wgpu::Buffer,
    buf_cb: wgpu::Buffer,
}

struct State {
    window: Arc<Window>,
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,

    off_tex: wgpu::Texture,
    off_view: wgpu::TextureView,

    spheres: SpheresGpu,
    cam_buf: wgpu::Buffer,
    cam: Camera,
    frame_index: u32,
    last_frame: Instant,

    compute_layout: wgpu::BindGroupLayout,
    compute_bind: wgpu::BindGroup,
    compute_pipeline: wgpu::ComputePipeline,

    render_layout: wgpu::BindGroupLayout,
    render_bind: wgpu::BindGroup,
    sampler: wgpu::Sampler,
    render_pipeline: wgpu::RenderPipeline,
}

#[derive(Clone, Copy)]
struct Camera {
    pos: Vec3A,
    yaw: f32,
    pitch: f32,
    fovy_deg: f32,
}

impl Camera {
    fn new() -> Self {
        Self { pos: CAMERA_START, yaw: 3.14159, pitch: 0.0, fovy_deg: 60.0 }
    }

    fn basis(&self) -> (Vec3A, Vec3A, Vec3A) {
        let (sy, cy) = self.yaw.sin_cos();
        let (sp, cp) = self.pitch.sin_cos();
        let forward = Vec3A::new(cy * cp, sp, -sy * cp).normalize();
        let right = forward.cross(Vec3A::Y).normalize_or_zero();
        let up = right.cross(forward).normalize_or_zero();
        (forward, right, up)
    }
}

#[derive(Default)]
struct InputState {
    w: bool,
    a: bool,
    s: bool,
    d: bool,
    q: bool,
    e: bool,
    shift: bool,
    ctrl: bool,
    mouse_locked: bool,
    mouse_dx: f32,
    mouse_dy: f32,
}

impl InputState {
    fn set_key(&mut self, code: KeyCode, pressed: bool) {
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

    fn consume_mouse_delta(&mut self) -> (f32, f32) {
        let delta = (self.mouse_dx, self.mouse_dy);
        self.mouse_dx = 0.0;
        self.mouse_dy = 0.0;
        delta
    }
}

struct Pcg32 {
    state: u32,
}

impl Pcg32 {
    fn new(seed: u32) -> Self {
        Self { state: seed }
    }

    fn next_u32(&mut self) -> u32 {
        let mut v = self.state;
        v = v.wrapping_mul(747_796_405).wrapping_add(2_891_336_453);
        let word = ((v >> ((v >> 28) + 4)) ^ v).wrapping_mul(277_803_737);
        let result = (word >> 22) ^ word;
        self.state = result;
        result
    }

    fn next_f32(&mut self) -> f32 {
        const SCALE: f32 = 1.0 / 4_294_967_296.0;
        self.next_u32() as f32 * SCALE
    }
}

impl State {
    async fn new(window: Arc<Window>) -> Self {
        let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor::default());
        let surface = instance.create_surface(window.clone()).expect("create surface");

        let adapter = instance
            .request_adapter(&wgpu::RequestAdapterOptions {
                power_preference: wgpu::PowerPreference::HighPerformance,
                compatible_surface: Some(&surface),
                force_fallback_adapter: false,
            })
            .await
            .expect("request adapter");

        let device_descriptor = wgpu::DeviceDescriptor {
            label: Some("Device"),
            required_features: wgpu::Features::empty(),
            required_limits: wgpu::Limits::default(),
            experimental_features: wgpu::ExperimentalFeatures::default(),
            memory_hints: wgpu::MemoryHints::Performance,
            trace: wgpu::Trace::default(),
        };
        let (device, queue) = adapter.request_device(&device_descriptor).await.expect("request device");

        let size = window.inner_size();
        let caps = surface.get_capabilities(&adapter);
        let format = caps
            .formats
            .iter()
            .copied()
            .find(TextureFormat::is_srgb)
            .unwrap_or_else(|| caps.formats[0]);
        let present_mode = if caps.present_modes.contains(&wgpu::PresentMode::Fifo) {
            wgpu::PresentMode::Fifo
        } else {
            caps.present_modes.first().copied().unwrap_or(wgpu::PresentMode::Fifo)
        };
        let alpha_mode = caps.alpha_modes.first().copied().unwrap_or(wgpu::CompositeAlphaMode::Auto);
        let config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format,
            width: size.width.max(1),
            height: size.height.max(1),
            present_mode,
            alpha_mode,
            view_formats: vec![],
            desired_maximum_frame_latency: 2,
        };
        surface.configure(&device, &config);

        let compute_module = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("compute"),
            source: make_spirv(COMPUTE_SPV),
        });
        let blit_vertex = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("blit-vert"),
            source: make_spirv(BLIT_VERT_SPV),
        });
        let blit_fragment = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("blit-frag"),
            source: make_spirv(BLIT_FRAG_SPV),
        });

        let (off_tex, off_view) = Self::create_offscreen(&device, config.width, config.height);

        let spheres = Self::create_spheres(&device);

        let cam = Camera::new();
        let cam_buf = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("CameraUBO"),
            size: std::mem::size_of::<CameraUbo>() as u64,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });

        let compute_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("compute-layout"),
            entries: &[
                wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: ShaderStages::COMPUTE,
                    ty: wgpu::BindingType::StorageTexture {
                        access: wgpu::StorageTextureAccess::WriteOnly,
                        format: wgpu::TextureFormat::Rgba8Unorm,
                        view_dimension: wgpu::TextureViewDimension::D2,
                    },
                    count: None,
                },
                bgl_storage(1),
                bgl_storage(2),
                bgl_storage(3),
                bgl_storage(4),
                bgl_storage(5),
                bgl_storage(6),
                bgl_storage(7),
                wgpu::BindGroupLayoutEntry {
                    binding: 8,
                    visibility: ShaderStages::COMPUTE,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
            ],
        });
        let compute_pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("compute-pipeline-layout"),
            bind_group_layouts: &[&compute_layout],
            push_constant_ranges: &[],
        });
        let compute_pipeline = device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
            label: Some("compute"),
            layout: Some(&compute_pipeline_layout),
            module: &compute_module,
            entry_point: Some("main"),
            compilation_options: wgpu::PipelineCompilationOptions::default(),
            cache: None,
        });
        let compute_bind = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("compute-bind"),
            layout: &compute_layout,
            entries: &[
                wgpu::BindGroupEntry { binding: 0, resource: wgpu::BindingResource::TextureView(&off_view) },
                bind_storage(1, &spheres.buf_cx),
                bind_storage(2, &spheres.buf_cy),
                bind_storage(3, &spheres.buf_cz),
                bind_storage(4, &spheres.buf_r),
                bind_storage(5, &spheres.buf_cr),
                bind_storage(6, &spheres.buf_cg),
                bind_storage(7, &spheres.buf_cb),
                wgpu::BindGroupEntry { binding: 8, resource: cam_buf.as_entire_binding() },
            ],
        });

        let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("blit-sampler"),
            address_mode_u: wgpu::AddressMode::ClampToEdge,
            address_mode_v: wgpu::AddressMode::ClampToEdge,
            address_mode_w: wgpu::AddressMode::ClampToEdge,
            mag_filter: wgpu::FilterMode::Linear,
            min_filter: wgpu::FilterMode::Linear,
            mipmap_filter: wgpu::FilterMode::Nearest,
            ..Default::default()
        });
        let render_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("render-layout"),
            entries: &[
                wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 1,
                    visibility: ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Texture {
                        sample_type: wgpu::TextureSampleType::Float { filterable: true },
                        view_dimension: wgpu::TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
            ],
        });
        let render_pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("render-pipeline-layout"),
            bind_group_layouts: &[&render_layout],
            push_constant_ranges: &[],
        });
        let render_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("render"),
            layout: Some(&render_pipeline_layout),
            vertex: wgpu::VertexState {
                module: &blit_vertex,
                entry_point: Some("main"),
                buffers: &[],
                compilation_options: wgpu::PipelineCompilationOptions::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: &blit_fragment,
                entry_point: Some("main"),
                targets: &[Some(wgpu::ColorTargetState {
                    format: config.format,
                    blend: Some(wgpu::BlendState::REPLACE),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
                compilation_options: wgpu::PipelineCompilationOptions::default(),
            }),
            primitive: wgpu::PrimitiveState::default(),
            depth_stencil: None,
            multisample: wgpu::MultisampleState::default(),
            multiview: None,
            cache: None,
        });
        let render_bind = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("render-bind"),
            layout: &render_layout,
            entries: &[
                wgpu::BindGroupEntry { binding: 0, resource: wgpu::BindingResource::Sampler(&sampler) },
                wgpu::BindGroupEntry { binding: 1, resource: wgpu::BindingResource::TextureView(&off_view) },
            ],
        });

        let mut state = Self {
            window,
            surface,
            device,
            queue,
            config,
            off_tex,
            off_view,
            spheres,
            cam_buf,
            cam,
            frame_index: 0,
            last_frame: Instant::now(),
            compute_layout,
            compute_bind,
            compute_pipeline,
            render_layout,
            render_bind,
            sampler,
            render_pipeline,
        };
        state.write_camera_ubo();
        state
    }

    fn create_offscreen(device: &wgpu::Device, width: u32, height: u32) -> (wgpu::Texture, wgpu::TextureView) {
        let texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("offscreen"),
            size: wgpu::Extent3d {
                width: width.max(1),
                height: height.max(1),
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::STORAGE_BINDING | wgpu::TextureUsages::TEXTURE_BINDING,
            view_formats: &[],
        });
        let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
        (texture, view)
    }

    fn create_spheres(device: &wgpu::Device) -> SpheresGpu {
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

            // Keep spheres clear of the camera start to avoid immediate intersections.
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

        let count = centers.len() as u32;

        let cx: Vec<f32> = centers.iter().map(|p| p.x).collect();
        let cy: Vec<f32> = centers.iter().map(|p| p.y).collect();
        let cz: Vec<f32> = centers.iter().map(|p| p.z).collect();
        let rr: Vec<f32> = radii.clone();
        let cr: Vec<f32> = colors.iter().map(|c| c.x).collect();
        let cg: Vec<f32> = colors.iter().map(|c| c.y).collect();
        let cb: Vec<f32> = colors.iter().map(|c| c.z).collect();

        let mk = |label: &str, data: &[u8]| device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some(label),
            contents: data,
            usage: wgpu::BufferUsages::STORAGE | wgpu::BufferUsages::COPY_DST,
        });

        SpheresGpu {
            count,
            buf_cx: mk("s.cx", bytemuck::cast_slice(&cx)),
            buf_cy: mk("s.cy", bytemuck::cast_slice(&cy)),
            buf_cz: mk("s.cz", bytemuck::cast_slice(&cz)),
            buf_r: mk("s.r", bytemuck::cast_slice(&rr)),
            buf_cr: mk("s.cr", bytemuck::cast_slice(&cr)),
            buf_cg: mk("s.cg", bytemuck::cast_slice(&cg)),
            buf_cb: mk("s.cb", bytemuck::cast_slice(&cb)),
        }
    }

    fn resize(&mut self, width: u32, height: u32) {
        if width == 0 || height == 0 {
            return;
        }
        self.config.width = width;
        self.config.height = height;
        self.surface.configure(&self.device, &self.config);
        self.recreate_offscreen_bindings();
    }

    fn recreate_offscreen_bindings(&mut self) {
        let (off_tex, off_view) = Self::create_offscreen(&self.device, self.config.width, self.config.height);
        self.off_tex = off_tex;
        self.off_view = off_view;

        self.compute_bind = self.device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("compute-bind-resized"),
            layout: &self.compute_layout,
            entries: &[
                wgpu::BindGroupEntry { binding: 0, resource: wgpu::BindingResource::TextureView(&self.off_view) },
                bind_storage(1, &self.spheres.buf_cx),
                bind_storage(2, &self.spheres.buf_cy),
                bind_storage(3, &self.spheres.buf_cz),
                bind_storage(4, &self.spheres.buf_r),
                bind_storage(5, &self.spheres.buf_cr),
                bind_storage(6, &self.spheres.buf_cg),
                bind_storage(7, &self.spheres.buf_cb),
                wgpu::BindGroupEntry { binding: 8, resource: self.cam_buf.as_entire_binding() },
            ],
        });
        self.render_bind = self.device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("render-bind-resized"),
            layout: &self.render_layout,
            entries: &[
                wgpu::BindGroupEntry { binding: 0, resource: wgpu::BindingResource::Sampler(&self.sampler) },
                wgpu::BindGroupEntry { binding: 1, resource: wgpu::BindingResource::TextureView(&self.off_view) },
            ],
        });
    }

    fn write_camera_ubo(&mut self) {
        let (fwd, right, up) = self.cam.basis();
        let aspect = self.config.width.max(1) as f32 / self.config.height.max(1) as f32;
        let tan_half = (0.5 * self.cam.fovy_deg.to_radians()).tan();
        let data = CameraUbo {
            cam_pos: self.cam.pos.to_array(),
            _pad0: 0.0,
            cam_fwd: fwd.to_array(),
            _pad1: 0.0,
            cam_right: right.to_array(),
            _pad2: 0.0,
            cam_up: up.to_array(),
            _pad3: 0.0,
            tan_half_fovy: tan_half,
            aspect,
            frame_index: self.frame_index,
            sphere_count: self.spheres.count,
        };
        self.queue.write_buffer(&self.cam_buf, 0, bytemuck::bytes_of(&data));
    }

    fn update_camera(&mut self, input: &mut InputState, dt: f32) {
        if input.mouse_locked {
            let (dx, dy) = input.consume_mouse_delta();
            let sensitivity = 0.0025;
            self.cam.yaw -= dx * sensitivity;
            self.cam.pitch -= dy * sensitivity;
            let limit = std::f32::consts::FRAC_PI_2 - 0.001;
            self.cam.pitch = self.cam.pitch.clamp(-limit, limit);
        }

        let (forward, right, _) = self.cam.basis();
        let mut velocity = Vec3A::ZERO;
        if input.w {
            velocity += forward;
        }
        if input.s {
            velocity -= forward;
        }
        if input.d {
            velocity += right;
        }
        if input.a {
            velocity -= right;
        }
        if input.e {
            velocity += Vec3A::Y;
        }
        if input.q {
            velocity -= Vec3A::Y;
        }

        if velocity.length_squared() > 0.0 {
            let base_speed = 3.0;
            let speed = if input.shift {
                base_speed * 3.0
            } else if input.ctrl {
                base_speed * 0.4
            } else {
                base_speed
            };
            self.cam.pos += velocity.normalize() * speed * dt;
        }
    }

    fn frame(&mut self, input: &mut InputState) {
        let now = Instant::now();
        let dt = (now - self.last_frame).as_secs_f32().max(1.0 / 600.0);
        self.last_frame = now;

        self.update_camera(input, dt);
        self.frame_index = self.frame_index.wrapping_add(1);
        self.write_camera_ubo();

        let frame = match self.surface.get_current_texture() {
            Ok(frame) => frame,
            Err(wgpu::SurfaceError::Lost | wgpu::SurfaceError::Outdated) => {
                let size = self.window.inner_size();
                self.resize(size.width, size.height);
                return;
            }
            Err(err) => {
                eprintln!("surface error: {err}");
                return;
            }
        };
        let frame_view = frame.texture.create_view(&wgpu::TextureViewDescriptor::default());

        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor { label: Some("encoder") });

        {
            let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
                label: Some("compute"),
                timestamp_writes: None,
            });
            pass.set_pipeline(&self.compute_pipeline);
            pass.set_bind_group(0, &self.compute_bind, &[]);
            let gx = (self.config.width + 7) / 8;
            let gy = (self.config.height + 7) / 8;
            pass.dispatch_workgroups(gx.max(1), gy.max(1), 1);
        }

        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("render-pass"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &frame_view,
                    resolve_target: None,
                    depth_slice: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store,
                    },
                })],
                depth_stencil_attachment: None,
                occlusion_query_set: None,
                timestamp_writes: None,
            });
            pass.set_pipeline(&self.render_pipeline);
            pass.set_bind_group(0, &self.render_bind, &[]);
            pass.draw(0..3, 0..1);
        }

        self.queue.submit(Some(encoder.finish()));
        frame.present();
        self.window.request_redraw();

        if self.frame_index % 60 == 0 {
            let ms = dt * 1_000.0;
            println!("frame {:>6} | {:>5.2} ms", self.frame_index, ms);
        }
    }
}

fn bgl_storage(binding: u32) -> wgpu::BindGroupLayoutEntry {
    wgpu::BindGroupLayoutEntry {
        binding,
        visibility: ShaderStages::COMPUTE,
        ty: wgpu::BindingType::Buffer {
            ty: wgpu::BufferBindingType::Storage { read_only: true },
            has_dynamic_offset: false,
            min_binding_size: None,
        },
        count: None,
    }
}

fn bind_storage(binding: u32, buffer: &wgpu::Buffer) -> wgpu::BindGroupEntry {
    wgpu::BindGroupEntry {
        binding,
        resource: buffer.as_entire_binding(),
    }
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

    fn window_event(&mut self, event_loop: &ActiveEventLoop, _id: winit::window::WindowId, event: WindowEvent) {
        let Some(state) = self.state.as_mut() else {
            return;
        };
        match event {
            WindowEvent::CloseRequested => event_loop.exit(),
            WindowEvent::Resized(size) => state.resize(size.width, size.height),
            WindowEvent::RedrawRequested => state.frame(&mut self.input),
            WindowEvent::MouseInput { state: winit::event::ElementState::Pressed, .. } => {
                if !self.input.mouse_locked {
                    let _ = state
                        .window
                        .set_cursor_grab(CursorGrabMode::Confined)
                        .or_else(|_| state.window.set_cursor_grab(CursorGrabMode::Locked));
                    state.window.set_cursor_visible(false);
                    self.input.mouse_locked = true;
                }
            }
            WindowEvent::KeyboardInput { event, .. } => {
                if let PhysicalKey::Code(code) = event.physical_key {
                    if code == KeyCode::Escape && event.state.is_pressed() {
                        let _ = state.window.set_cursor_grab(CursorGrabMode::None);
                        state.window.set_cursor_visible(true);
                        self.input.mouse_locked = false;
                    } else {
                        self.input.set_key(code, event.state.is_pressed());
                    }
                }
            }
            _ => {}
        }
    }

    fn device_event(&mut self, _event_loop: &ActiveEventLoop, _device_id: winit::event::DeviceId, event: DeviceEvent) {
        if let DeviceEvent::MouseMotion { delta } = event {
            if self.input.mouse_locked {
                self.input.mouse_dx += delta.0 as f32;
                self.input.mouse_dy += delta.1 as f32;
            }
        }
    }
}

fn main() {
    let event_loop = EventLoop::new().expect("event loop");
    let mut app = App::default();
    event_loop.run_app(&mut app).expect("run app");
}
