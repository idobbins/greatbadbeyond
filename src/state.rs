use std::sync::Arc;
use std::time::Instant;

use bytemuck::{Pod, Zeroable};
use glam::Vec3A;
use wgpu::{ShaderStages, TextureFormat, util::make_spirv};
use winit::window::Window;

use crate::camera::Camera;
use crate::input::InputState;
use crate::scene::{SpheresGpu, create_spheres};

const COMPUTE_SPV: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/compute.comp.glsl.spv"));
const BLIT_VERT_SPV: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/blit.vert.glsl.spv"));
const BLIT_FRAG_SPV: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/blit.frag.glsl.spv"));

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

pub struct State {
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

impl State {
    pub async fn new(window: Arc<Window>) -> Self {
        let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor::default());
        let surface = instance
            .create_surface(window.clone())
            .expect("create surface");

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
        let (device, queue) = adapter
            .request_device(&device_descriptor)
            .await
            .expect("request device");

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
            caps.present_modes
                .first()
                .copied()
                .unwrap_or(wgpu::PresentMode::Fifo)
        };
        let alpha_mode = caps
            .alpha_modes
            .first()
            .copied()
            .unwrap_or(wgpu::CompositeAlphaMode::Auto);
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

        let spheres = create_spheres(&device);

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
                wgpu::BindGroupLayoutEntry {
                    binding: 3,
                    visibility: ShaderStages::COMPUTE,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 4,
                    visibility: ShaderStages::COMPUTE,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
                bgl_storage(5),
                bgl_storage(6),
                bgl_storage(7),
            ],
        });
        let compute_pipeline_layout =
            device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
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
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&off_view),
                },
                bind_storage(1, &spheres.buf_center_radius),
                bind_storage(2, &spheres.buf_albedo),
                wgpu::BindGroupEntry {
                    binding: 3,
                    resource: cam_buf.as_entire_binding(),
                },
                wgpu::BindGroupEntry {
                    binding: 4,
                    resource: spheres.buf_grid_params.as_entire_binding(),
                },
                bind_storage(5, &spheres.buf_l0_cells),
                bind_storage(6, &spheres.buf_l1_cells),
                bind_storage(7, &spheres.buf_cell_indices),
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
        let render_pipeline_layout =
            device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
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
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::Sampler(&sampler),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&off_view),
                },
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

    pub fn window(&self) -> &Window {
        &self.window
    }

    pub fn resize(&mut self, width: u32, height: u32) {
        if width == 0 || height == 0 {
            return;
        }
        self.config.width = width;
        self.config.height = height;
        self.surface.configure(&self.device, &self.config);
        self.recreate_offscreen_bindings();
    }

    pub fn frame(&mut self, input: &mut InputState) {
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
        let frame_view = frame
            .texture
            .create_view(&wgpu::TextureViewDescriptor::default());

        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("encoder"),
            });

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

    fn create_offscreen(
        device: &wgpu::Device,
        width: u32,
        height: u32,
    ) -> (wgpu::Texture, wgpu::TextureView) {
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

    fn recreate_offscreen_bindings(&mut self) {
        let (off_tex, off_view) =
            Self::create_offscreen(&self.device, self.config.width, self.config.height);
        self.off_tex = off_tex;
        self.off_view = off_view;

        self.compute_bind = self.device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("compute-bind-resized"),
            layout: &self.compute_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&self.off_view),
                },
                bind_storage(1, &self.spheres.buf_center_radius),
                bind_storage(2, &self.spheres.buf_albedo),
                wgpu::BindGroupEntry {
                    binding: 3,
                    resource: self.cam_buf.as_entire_binding(),
                },
                wgpu::BindGroupEntry {
                    binding: 4,
                    resource: self.spheres.buf_grid_params.as_entire_binding(),
                },
                bind_storage(5, &self.spheres.buf_l0_cells),
                bind_storage(6, &self.spheres.buf_l1_cells),
                bind_storage(7, &self.spheres.buf_cell_indices),
            ],
        });
        self.render_bind = self.device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("render-bind-resized"),
            layout: &self.render_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::Sampler(&self.sampler),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&self.off_view),
                },
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
        self.queue
            .write_buffer(&self.cam_buf, 0, bytemuck::bytes_of(&data));
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
