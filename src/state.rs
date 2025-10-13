use std::sync::Arc;
use std::time::Instant;

use bytemuck::{Pod, Zeroable};
use glam::Vec3A;
use wgpu::{ShaderStages, TextureFormat, util::make_spirv};
use winit::window::Window;

use crate::camera::Camera;
use crate::input::InputState;
use crate::metrics::{TimingStats, TimingSummary};
use crate::scene::{SpheresGpu, create_spheres};

const COMPUTE_SPV: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/compute.comp.glsl.spv"));
const ACCUM_SPV: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/denoise.comp.glsl.spv"));
const BLIT_VERT_SPV: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/blit.vert.glsl.spv"));
const BLIT_FRAG_SPV: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/blit.frag.glsl.spv"));

const TIMING_SAMPLE_INTERVAL: u32 = 60;
const TIMING_HISTORY: usize = 600;
const WINDOW_TITLE_BASE: &str = "Callandor";
const DEFAULT_HISTORY_WEIGHT: f32 = 1.0;
const RESET_HISTORY_WEIGHT: f32 = 0.0;
const MOVING_HISTORY_WEIGHT: f32 = 0.2;

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

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable, Debug)]
struct AccumParams {
    history_weight: f32,
    _pad: [f32; 3],
}

pub struct State {
    window: Arc<Window>,
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,

    trace_tex: wgpu::Texture,
    trace_view: wgpu::TextureView,
    accum_tex_a: wgpu::Texture,
    accum_view_a: wgpu::TextureView,
    accum_tex_b: wgpu::Texture,
    accum_view_b: wgpu::TextureView,
    history_is_a: bool,

    spheres: SpheresGpu,
    cam_buf: wgpu::Buffer,
    accum_params_buf: wgpu::Buffer,
    cam: Camera,
    frame_index: u32,
    last_frame: Instant,
    timing: TimingStats,
    history_weight: f32,

    compute_layout: wgpu::BindGroupLayout,
    compute_bind: wgpu::BindGroup,
    compute_pipeline: wgpu::ComputePipeline,

    accumulate_layout: wgpu::BindGroupLayout,
    accumulate_bind_a: wgpu::BindGroup,
    accumulate_bind_b: wgpu::BindGroup,
    accumulate_pipeline: wgpu::ComputePipeline,

    render_layout: wgpu::BindGroupLayout,
    render_bind_a: wgpu::BindGroup,
    render_bind_b: wgpu::BindGroup,
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

        window.set_title(WINDOW_TITLE_BASE);

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
        let accumulate_module = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("accumulate"),
            source: make_spirv(ACCUM_SPV),
        });
        let blit_vertex = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("blit-vert"),
            source: make_spirv(BLIT_VERT_SPV),
        });
        let blit_fragment = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("blit-frag"),
            source: make_spirv(BLIT_FRAG_SPV),
        });

        let (trace_tex, trace_view) =
            Self::create_trace_texture(&device, config.width, config.height);
        let (accum_tex_a, accum_view_a) =
            Self::create_accum_texture(&device, config.width, config.height, "accum-a");
        let (accum_tex_b, accum_view_b) =
            Self::create_accum_texture(&device, config.width, config.height, "accum-b");

        let spheres = create_spheres(&device);

        let cam = Camera::new();
        let cam_buf = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("CameraUBO"),
            size: std::mem::size_of::<CameraUbo>() as u64,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let accum_params_buf = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("AccumParams"),
            size: std::mem::size_of::<AccumParams>() as u64,
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
                        format: wgpu::TextureFormat::Rgba32Float,
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
                    resource: wgpu::BindingResource::TextureView(&trace_view),
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

        let accumulate_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("accumulate-layout"),
            entries: &[
                wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: ShaderStages::COMPUTE,
                    ty: wgpu::BindingType::StorageTexture {
                        access: wgpu::StorageTextureAccess::ReadOnly,
                        format: wgpu::TextureFormat::Rgba16Float,
                        view_dimension: wgpu::TextureViewDimension::D2,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 1,
                    visibility: ShaderStages::COMPUTE,
                    ty: wgpu::BindingType::StorageTexture {
                        access: wgpu::StorageTextureAccess::ReadOnly,
                        format: wgpu::TextureFormat::Rgba32Float,
                        view_dimension: wgpu::TextureViewDimension::D2,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 2,
                    visibility: ShaderStages::COMPUTE,
                    ty: wgpu::BindingType::StorageTexture {
                        access: wgpu::StorageTextureAccess::WriteOnly,
                        format: wgpu::TextureFormat::Rgba16Float,
                        view_dimension: wgpu::TextureViewDimension::D2,
                    },
                    count: None,
                },
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
            ],
        });
        let accumulate_pipeline_layout =
            device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
                label: Some("accumulate-pipeline-layout"),
                bind_group_layouts: &[&accumulate_layout],
                push_constant_ranges: &[],
            });
        let accumulate_pipeline =
            device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
                label: Some("accumulate"),
                layout: Some(&accumulate_pipeline_layout),
                module: &accumulate_module,
                entry_point: Some("main"),
                compilation_options: wgpu::PipelineCompilationOptions::default(),
                cache: None,
            });
        let accumulate_bind_a = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("accumulate-bind-a"),
            layout: &accumulate_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&accum_view_a),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&trace_view),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: wgpu::BindingResource::TextureView(&accum_view_b),
                },
                wgpu::BindGroupEntry {
                    binding: 3,
                    resource: accum_params_buf.as_entire_binding(),
                },
            ],
        });
        let accumulate_bind_b = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("accumulate-bind-b"),
            layout: &accumulate_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&accum_view_b),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&trace_view),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: wgpu::BindingResource::TextureView(&accum_view_a),
                },
                wgpu::BindGroupEntry {
                    binding: 3,
                    resource: accum_params_buf.as_entire_binding(),
                },
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
        let render_bind_a = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("render-bind-a"),
            layout: &render_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::Sampler(&sampler),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&accum_view_a),
                },
            ],
        });
        let render_bind_b = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("render-bind-b"),
            layout: &render_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::Sampler(&sampler),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&accum_view_b),
                },
            ],
        });

        let timing = TimingStats::new(TIMING_SAMPLE_INTERVAL, TIMING_HISTORY);

        let mut state = Self {
            window,
            surface,
            device,
            queue,
            config,
            trace_tex,
            trace_view,
            accum_tex_a,
            accum_view_a,
            accum_tex_b,
            accum_view_b,
            history_is_a: true,
            spheres,
            cam_buf,
            accum_params_buf,
            cam,
            frame_index: 0,
            last_frame: Instant::now(),
            timing,
            history_weight: DEFAULT_HISTORY_WEIGHT,
            compute_layout,
            compute_bind,
            compute_pipeline,
            accumulate_layout,
            accumulate_bind_a,
            accumulate_bind_b,
            accumulate_pipeline,
            render_layout,
            render_bind_a,
            render_bind_b,
            sampler,
            render_pipeline,
        };
        state.write_camera_ubo();
        state.write_accum_params();
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
        self.recreate_textures_and_bindings();
        self.frame_index = 0;
        self.history_weight = RESET_HISTORY_WEIGHT;
        self.write_accum_params();
    }

    pub fn frame(&mut self, input: &mut InputState) {
        let now = Instant::now();
        let dt = (now - self.last_frame).as_secs_f32().max(1.0 / 600.0);
        self.last_frame = now;

        let camera_moved = self.update_camera(input, dt);
        if camera_moved {
            self.history_weight = MOVING_HISTORY_WEIGHT;
        }
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

        self.write_accum_params();

        let frame_start = Instant::now();

        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("encoder"),
            });

        let gx = (self.config.width + 7) / 8;
        let gy = (self.config.height + 7) / 8;
        let dispatch_x = gx.max(1);
        let dispatch_y = gy.max(1);

        let history_is_a = self.history_is_a;

        {
            let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
                label: Some("path-trace"),
                timestamp_writes: None,
            });
            pass.set_pipeline(&self.compute_pipeline);
            pass.set_bind_group(0, &self.compute_bind, &[]);
            pass.dispatch_workgroups(dispatch_x, dispatch_y, 1);
        }
        {
            let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
                label: Some("accumulate"),
                timestamp_writes: None,
            });
            let accumulate_bind = if history_is_a {
                &self.accumulate_bind_a
            } else {
                &self.accumulate_bind_b
            };
            pass.set_pipeline(&self.accumulate_pipeline);
            pass.set_bind_group(0, accumulate_bind, &[]);
            pass.dispatch_workgroups(dispatch_x, dispatch_y, 1);
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
            let render_bind = if history_is_a {
                &self.render_bind_b
            } else {
                &self.render_bind_a
            };
            pass.set_bind_group(0, render_bind, &[]);
            pass.draw(0..3, 0..1);
        }

        self.queue.submit(Some(encoder.finish()));
        frame.present();
        self.rotate_history_targets();
        self.window.request_redraw();

        let _ = self.device.poll(wgpu::PollType::wait_indefinitely());
        let total_ms = frame_start.elapsed().as_secs_f32() * 1_000.0;
        self.consume_timing_sample(total_ms);
    }

    fn create_trace_texture(
        device: &wgpu::Device,
        width: u32,
        height: u32,
    ) -> (wgpu::Texture, wgpu::TextureView) {
        let texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("trace-buffer"),
            size: wgpu::Extent3d {
                width: width.max(1),
                height: height.max(1),
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba32Float,
            usage: wgpu::TextureUsages::STORAGE_BINDING
                | wgpu::TextureUsages::TEXTURE_BINDING
                | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });
        let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
        (texture, view)
    }

    fn create_accum_texture(
        device: &wgpu::Device,
        width: u32,
        height: u32,
        label: &str,
    ) -> (wgpu::Texture, wgpu::TextureView) {
        let texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some(label),
            size: wgpu::Extent3d {
                width: width.max(1),
                height: height.max(1),
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba16Float,
            usage: wgpu::TextureUsages::STORAGE_BINDING
                | wgpu::TextureUsages::TEXTURE_BINDING
                | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });
        let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
        (texture, view)
    }

    fn recreate_textures_and_bindings(&mut self) {
        let (trace_tex, trace_view) =
            Self::create_trace_texture(&self.device, self.config.width, self.config.height);
        self.trace_tex = trace_tex;
        self.trace_view = trace_view;

        let (accum_tex_a, accum_view_a) = Self::create_accum_texture(
            &self.device,
            self.config.width,
            self.config.height,
            "accum-a",
        );
        let (accum_tex_b, accum_view_b) = Self::create_accum_texture(
            &self.device,
            self.config.width,
            self.config.height,
            "accum-b",
        );
        self.accum_tex_a = accum_tex_a;
        self.accum_view_a = accum_view_a;
        self.accum_tex_b = accum_tex_b;
        self.accum_view_b = accum_view_b;

        self.compute_bind = self.device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("compute-bind-resized"),
            layout: &self.compute_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&self.trace_view),
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

        self.accumulate_bind_a = self.device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("accumulate-bind-a-resized"),
            layout: &self.accumulate_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&self.accum_view_a),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&self.trace_view),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: wgpu::BindingResource::TextureView(&self.accum_view_b),
                },
                wgpu::BindGroupEntry {
                    binding: 3,
                    resource: self.accum_params_buf.as_entire_binding(),
                },
            ],
        });
        self.accumulate_bind_b = self.device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("accumulate-bind-b-resized"),
            layout: &self.accumulate_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&self.accum_view_b),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&self.trace_view),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: wgpu::BindingResource::TextureView(&self.accum_view_a),
                },
                wgpu::BindGroupEntry {
                    binding: 3,
                    resource: self.accum_params_buf.as_entire_binding(),
                },
            ],
        });

        self.render_bind_a = self.device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("render-bind-a-resized"),
            layout: &self.render_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::Sampler(&self.sampler),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&self.accum_view_a),
                },
            ],
        });
        self.render_bind_b = self.device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("render-bind-b-resized"),
            layout: &self.render_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::Sampler(&self.sampler),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&self.accum_view_b),
                },
            ],
        });

        self.history_is_a = true;
        self.history_weight = RESET_HISTORY_WEIGHT;
    }

    fn rotate_history_targets(&mut self) {
        self.history_is_a = !self.history_is_a;
    }

    fn consume_timing_sample(&mut self, total_ms: f32) {
        if let Some(summary) = self.timing.record(total_ms) {
            self.update_window_title(&summary);
        }
    }

    fn update_window_title(&self, summary: &TimingSummary) {
        let title = format!(
            "{WINDOW_TITLE_BASE} | frame {t:.2} ms | avg {at:.2} ms | p95 {p95:.2} ms | p99 {p99:.2} ms | n={n}",
            t = summary.last_total,
            at = summary.avg_total,
            p95 = summary.p95_total,
            p99 = summary.p99_total,
            n = summary.sample_count,
        );
        self.window.set_title(&title);
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

    fn write_accum_params(&mut self) {
        let weight = self.history_weight.clamp(0.0, 1.0);
        let params = AccumParams {
            history_weight: weight,
            _pad: [0.0; 3],
        };
        self.queue
            .write_buffer(&self.accum_params_buf, 0, bytemuck::bytes_of(&params));
        self.history_weight = DEFAULT_HISTORY_WEIGHT;
    }

    fn update_camera(&mut self, input: &mut InputState, dt: f32) -> bool {
        let prev_pos = self.cam.pos;
        let prev_yaw = self.cam.yaw;
        let prev_pitch = self.cam.pitch;

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

        let moved_pos = (self.cam.pos - prev_pos).length_squared() > 1e-6;
        let moved_yaw = (self.cam.yaw - prev_yaw).abs() > 1e-6;
        let moved_pitch = (self.cam.pitch - prev_pitch).abs() > 1e-6;
        moved_pos || moved_yaw || moved_pitch
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
