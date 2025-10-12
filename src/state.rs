use std::sync::{Arc, mpsc};
use std::time::Instant;

use bytemuck::{Pod, Zeroable};
use glam::Vec3A;
use wgpu::{ShaderStages, TextureFormat, util::make_spirv};
use winit::window::Window;

use crate::camera::Camera;
use crate::input::InputState;
use crate::metrics::{FrameDurations, TimingStats, TimingSummary};
use crate::scene::{SpheresGpu, create_spheres};

const COMPUTE_SPV: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/compute.comp.glsl.spv"));
const DENOISE_SPV: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/denoise.comp.glsl.spv"));
const BLIT_VERT_SPV: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/blit.vert.glsl.spv"));
const BLIT_FRAG_SPV: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/blit.frag.glsl.spv"));

const TIMING_SAMPLE_INTERVAL: u32 = 60;
const TIMING_HISTORY: usize = 600;
const TIMING_BUFFER_COUNT: usize = 4;
const WINDOW_TITLE_BASE: &str = "Callandor";
const TIMESTAMP_COUNT: u32 = 4;

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
    trace_tex: wgpu::Texture,
    trace_view: wgpu::TextureView,

    spheres: SpheresGpu,
    cam_buf: wgpu::Buffer,
    cam: Camera,
    frame_index: u32,
    last_frame: Instant,
    timing: TimingStats,
    gpu_timers: Option<GpuTimers>,

    compute_layout: wgpu::BindGroupLayout,
    compute_bind: wgpu::BindGroup,
    compute_pipeline: wgpu::ComputePipeline,

    denoise_layout: wgpu::BindGroupLayout,
    denoise_bind: wgpu::BindGroup,
    denoise_pipeline: wgpu::ComputePipeline,

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

        let adapter_features = adapter.features();
        let mut supports_gpu_timers = adapter_features.contains(wgpu::Features::TIMESTAMP_QUERY)
            && adapter_features.contains(wgpu::Features::TIMESTAMP_QUERY_INSIDE_ENCODERS);

        let mut required_features = wgpu::Features::empty();
        if supports_gpu_timers {
            required_features |= wgpu::Features::TIMESTAMP_QUERY;
            required_features |= wgpu::Features::TIMESTAMP_QUERY_INSIDE_ENCODERS;
        }

        let device_descriptor = wgpu::DeviceDescriptor {
            label: Some("Device"),
            required_features,
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
        let device_features = device.features();
        supports_gpu_timers = supports_gpu_timers
            && device_features.contains(wgpu::Features::TIMESTAMP_QUERY)
            && device_features.contains(wgpu::Features::TIMESTAMP_QUERY_INSIDE_ENCODERS);

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
        let denoise_module = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("denoise"),
            source: make_spirv(DENOISE_SPV),
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

        let denoise_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("denoise-layout"),
            entries: &[
                wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: ShaderStages::COMPUTE,
                    ty: wgpu::BindingType::StorageTexture {
                        access: wgpu::StorageTextureAccess::ReadOnly,
                        format: wgpu::TextureFormat::Rgba32Float,
                        view_dimension: wgpu::TextureViewDimension::D2,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 1,
                    visibility: ShaderStages::COMPUTE,
                    ty: wgpu::BindingType::StorageTexture {
                        access: wgpu::StorageTextureAccess::WriteOnly,
                        format: wgpu::TextureFormat::Rgba8Unorm,
                        view_dimension: wgpu::TextureViewDimension::D2,
                    },
                    count: None,
                },
            ],
        });
        let denoise_pipeline_layout =
            device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
                label: Some("denoise-pipeline-layout"),
                bind_group_layouts: &[&denoise_layout],
                push_constant_ranges: &[],
            });
        let denoise_pipeline = device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
            label: Some("denoise"),
            layout: Some(&denoise_pipeline_layout),
            module: &denoise_module,
            entry_point: Some("main"),
            compilation_options: wgpu::PipelineCompilationOptions::default(),
            cache: None,
        });
        let denoise_bind = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("denoise-bind"),
            layout: &denoise_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&trace_view),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&off_view),
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

        let timing = TimingStats::new(TIMING_SAMPLE_INTERVAL, TIMING_HISTORY);
        let gpu_timers = if supports_gpu_timers {
            Some(GpuTimers::new(&device, &queue, TIMING_BUFFER_COUNT))
        } else {
            None
        };

        let mut state = Self {
            window,
            surface,
            device,
            queue,
            config,
            trace_tex,
            trace_view,
            off_tex,
            off_view,
            spheres,
            cam_buf,
            cam,
            frame_index: 0,
            last_frame: Instant::now(),
            timing,
            gpu_timers,
            compute_layout,
            compute_bind,
            compute_pipeline,
            denoise_layout,
            denoise_bind,
            denoise_pipeline,
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
        self.process_timing_samples();

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

        let measure_cpu = self.gpu_timers.is_none();
        let cpu_frame_start = if measure_cpu {
            Some(Instant::now())
        } else {
            None
        };
        let mut cpu_render_ms = 0.0f32;
        let mut cpu_denoise_ms = 0.0f32;

        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("encoder"),
            });

        let gx = (self.config.width + 7) / 8;
        let gy = (self.config.height + 7) / 8;
        let dispatch_x = gx.max(1);
        let dispatch_y = gy.max(1);

        let mut timer_frame = self
            .gpu_timers
            .as_mut()
            .and_then(|timers| timers.begin_frame());
        if let Some(frame_timer) = timer_frame.as_mut() {
            frame_timer.mark(&mut encoder, TimerPoint::FrameBegin);
        }

        let cpu_render_start = if measure_cpu {
            Some(Instant::now())
        } else {
            None
        };
        {
            let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
                label: Some("path-trace"),
                timestamp_writes: None,
            });
            pass.set_pipeline(&self.compute_pipeline);
            pass.set_bind_group(0, &self.compute_bind, &[]);
            pass.dispatch_workgroups(dispatch_x, dispatch_y, 1);
        }
        if let Some(start) = cpu_render_start {
            cpu_render_ms = start.elapsed().as_secs_f32() * 1_000.0;
        }
        if let Some(frame_timer) = timer_frame.as_mut() {
            frame_timer.mark(&mut encoder, TimerPoint::PathEnd);
            frame_timer.mark(&mut encoder, TimerPoint::DenoiseBegin);
        }

        let cpu_denoise_start = if measure_cpu {
            Some(Instant::now())
        } else {
            None
        };
        {
            let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
                label: Some("denoise"),
                timestamp_writes: None,
            });
            pass.set_pipeline(&self.denoise_pipeline);
            pass.set_bind_group(0, &self.denoise_bind, &[]);
            pass.dispatch_workgroups(dispatch_x, dispatch_y, 1);
        }
        if let Some(start) = cpu_denoise_start {
            cpu_denoise_ms = start.elapsed().as_secs_f32() * 1_000.0;
        }
        if let Some(mut frame_timer) = timer_frame {
            frame_timer.mark(&mut encoder, TimerPoint::FrameEnd);
            frame_timer.finish(&mut encoder);
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

        if let Some(frame_start) = cpu_frame_start {
            let total_ms = frame_start.elapsed().as_secs_f32() * 1_000.0;
            let sample = FrameDurations::new(cpu_render_ms, cpu_denoise_ms, total_ms);
            self.consume_timing_sample(sample);
        }
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
            usage: wgpu::TextureUsages::STORAGE_BINDING | wgpu::TextureUsages::TEXTURE_BINDING,
            view_formats: &[],
        });
        let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
        (texture, view)
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
        let (trace_tex, trace_view) =
            Self::create_trace_texture(&self.device, self.config.width, self.config.height);
        self.trace_tex = trace_tex;
        self.trace_view = trace_view;

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
        self.denoise_bind = self.device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("denoise-bind-resized"),
            layout: &self.denoise_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&self.trace_view),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&self.off_view),
                },
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

    fn process_timing_samples(&mut self) {
        let mut ready = Vec::new();
        if let Some(timers) = self.gpu_timers.as_mut() {
            while let Some(sample) = timers.collect_ready(&self.device, &self.queue) {
                ready.push(sample);
            }
        }
        for sample in ready {
            self.consume_timing_sample(sample);
        }
    }

    fn consume_timing_sample(&mut self, sample: FrameDurations) {
        if let Some(summary) = self.timing.record(sample) {
            self.update_window_title(&summary);
        }
    }

    fn update_window_title(&self, summary: &TimingSummary) {
        let title = format!(
            "{WINDOW_TITLE_BASE} | render {r:.2} ms | denoise {d:.2} ms | total {t:.2} ms | avg {ar:.2}/{ad:.2}/{at:.2} ms | p95 {p:.2} ms | n={n}",
            r = summary.last.render_ms,
            d = summary.last.denoise_ms,
            t = summary.last.total_ms,
            ar = summary.avg_render,
            ad = summary.avg_denoise,
            at = summary.avg_total,
            p = summary.p95_total,
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

struct GpuTimers {
    query_set: wgpu::QuerySet,
    buffers: Vec<wgpu::Buffer>,
    pending: Vec<bool>,
    write_index: usize,
    read_index: usize,
    period_ms: f32,
    buffer_size: wgpu::BufferAddress,
}

impl GpuTimers {
    fn new(device: &wgpu::Device, queue: &wgpu::Queue, slot_count: usize) -> Self {
        let query_set = device.create_query_set(&wgpu::QuerySetDescriptor {
            label: Some("timings-query-set"),
            ty: wgpu::QueryType::Timestamp,
            count: TIMESTAMP_COUNT,
        });

        let buffer_size = TIMESTAMP_COUNT as u64 * std::mem::size_of::<u64>() as u64;
        let buffers = (0..slot_count)
            .map(|i| {
                device.create_buffer(&wgpu::BufferDescriptor {
                    label: Some(&format!("timings-buffer-{i}")),
                    size: buffer_size,
                    usage: wgpu::BufferUsages::COPY_SRC | wgpu::BufferUsages::QUERY_RESOLVE,
                    mapped_at_creation: false,
                })
            })
            .collect();

        Self {
            query_set,
            buffers,
            pending: vec![false; slot_count],
            write_index: 0,
            read_index: 0,
            period_ms: queue.get_timestamp_period() / 1_000_000.0,
            buffer_size,
        }
    }

    fn begin_frame(&mut self) -> Option<GpuTimerFrame<'_>> {
        if self.buffers.is_empty() {
            return None;
        }
        let idx = self.write_index;
        if self.pending[idx] {
            return None;
        }
        self.write_index = (self.write_index + 1) % self.buffers.len();
        Some(GpuTimerFrame {
            timers: self,
            buffer_index: idx,
        })
    }

    fn collect_ready(
        &mut self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
    ) -> Option<FrameDurations> {
        if self.buffers.is_empty() {
            return None;
        }

        for _ in 0..self.buffers.len() {
            let idx = self.read_index;
            if !self.pending[idx] {
                self.read_index = (self.read_index + 1) % self.buffers.len();
                continue;
            }

            let buffer = &self.buffers[idx];
            let readback = device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("timings-readback"),
                size: self.buffer_size,
                usage: wgpu::BufferUsages::MAP_READ | wgpu::BufferUsages::COPY_DST,
                mapped_at_creation: false,
            });
            let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("timings-copy"),
            });
            encoder.copy_buffer_to_buffer(buffer, 0, &readback, 0, self.buffer_size);
            queue.submit(Some(encoder.finish()));

            let read_slice = readback.slice(..);
            let (sender, receiver) = mpsc::channel();
            read_slice.map_async(wgpu::MapMode::Read, move |result| {
                let _ = sender.send(result);
            });
            let _ = device.poll(wgpu::PollType::wait_indefinitely());

            let Ok(result) = receiver.recv() else {
                self.pending[idx] = false;
                self.read_index = (self.read_index + 1) % self.buffers.len();
                continue;
            };

            if result.is_err() {
                self.pending[idx] = false;
                self.read_index = (self.read_index + 1) % self.buffers.len();
                continue;
            }

            let data = read_slice.get_mapped_range();
            let timestamps: &[u64] = bytemuck::cast_slice(&data);
            let render_ticks = timestamps
                .get(1)
                .zip(timestamps.get(0))
                .map(|(end, start)| end.saturating_sub(*start))
                .unwrap_or(0);
            let denoise_ticks = timestamps
                .get(3)
                .zip(timestamps.get(2))
                .map(|(end, start)| end.saturating_sub(*start))
                .unwrap_or(0);
            let total_ticks = timestamps
                .get(3)
                .zip(timestamps.get(0))
                .map(|(end, start)| end.saturating_sub(*start))
                .unwrap_or(0);
            drop(data);
            readback.unmap();

            self.pending[idx] = false;
            self.read_index = (self.read_index + 1) % self.buffers.len();

            let render_ms = render_ticks as f32 * self.period_ms;
            let denoise_ms = denoise_ticks as f32 * self.period_ms;
            let total_ms = total_ticks as f32 * self.period_ms;
            return Some(FrameDurations::new(render_ms, denoise_ms, total_ms));
        }

        None
    }
}

struct GpuTimerFrame<'a> {
    timers: &'a mut GpuTimers,
    buffer_index: usize,
}

impl<'a> GpuTimerFrame<'a> {
    fn mark(&mut self, encoder: &mut wgpu::CommandEncoder, point: TimerPoint) {
        encoder.write_timestamp(&self.timers.query_set, point as u32);
    }

    fn finish(self, encoder: &mut wgpu::CommandEncoder) {
        let idx = self.buffer_index;
        encoder.resolve_query_set(
            &self.timers.query_set,
            0..TIMESTAMP_COUNT,
            &self.timers.buffers[idx],
            0,
        );
        self.timers.pending[idx] = true;
    }
}

#[repr(u32)]
#[derive(Clone, Copy)]
enum TimerPoint {
    FrameBegin = 0,
    PathEnd = 1,
    DenoiseBegin = 2,
    FrameEnd = 3,
}
