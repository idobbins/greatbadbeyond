use std::{
    f32::consts::{FRAC_PI_2, PI, TAU},
    sync::Arc,
};

use anyhow::{bail, Result};
use bytemuck::{Pod, Zeroable};
use pollster::FutureExt;
use wgpu::{
    include_wgsl, Backends, BindGroup, BindGroupDescriptor, BindGroupEntry, BindGroupLayout,
    BindGroupLayoutDescriptor, BindGroupLayoutEntry, BindingResource, BindingType, Buffer,
    BufferBindingType, BufferDescriptor, BufferUsages, Color, ColorTargetState, ColorWrites,
    CommandEncoderDescriptor, ComputePassDescriptor, ComputePipeline, ComputePipelineDescriptor,
    Device, DeviceDescriptor, ExperimentalFeatures, Extent3d, Features, FragmentState, Instance,
    InstanceDescriptor, Limits, LoadOp, MemoryHints, MultisampleState, Operations,
    PipelineCompilationOptions, PipelineLayoutDescriptor, PowerPreference, PresentMode,
    PrimitiveState, Queue, RenderPassColorAttachment, RenderPassDescriptor, RenderPipeline,
    RenderPipelineDescriptor, RequestAdapterOptions, Sampler, SamplerBindingType, SamplerDescriptor,
    ShaderStages, StorageTextureAccess, StoreOp, Surface, SurfaceConfiguration, SurfaceError,
    Texture, TextureDescriptor, TextureDimension, TextureFormat, TextureSampleType, TextureUsages,
    TextureView, TextureViewDescriptor, TextureViewDimension, Trace, VertexState,
};
use winit::{dpi::PhysicalSize, event_loop::ActiveEventLoop, window::Window};

#[inline]
fn ceil_div(x: u32, d: u32) -> u32 {
    (x + d - 1) / d
}

const SPHERE_COUNT: u32 = 256;

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct Params {
    cam_pos: [f32; 3],
    sphere_count: u32,
    cam_fwd: [f32; 3],
    _pad0: u32,
    cam_right: [f32; 3],
    _pad1: u32,
    cam_up: [f32; 3],
    _pad2: u32,
    half_fov_tan: f32,
    aspect: f32,
    ground_y: f32,
    ambient: f32,
    light_dir: [f32; 3],
    _pad3: u32,
    light_color: [f32; 3],
    _pad4: u32,
}

#[derive(Clone, Copy, Default)]
pub struct CameraInput {
    pub forward: f32,
    pub strafe: f32,
    pub ascend: f32,
    pub boost: bool,
}

pub struct Gfx {
    window: Arc<Window>,
    _instance: Instance,
    surface: Surface<'static>,
    device: Device,
    queue: Queue,
    surface_cfg: SurfaceConfiguration,
    _surface_format: TextureFormat,

    _storage_tex: Texture,
    storage_view: TextureView,

    // compute
    gen_pip: ComputePipeline,
    trace_pip: ComputePipeline,
    compute_bgl: BindGroupLayout,
    compute_bg: BindGroup,

    // render
    render_pip: RenderPipeline,
    render_bgl: BindGroupLayout,
    render_bg: BindGroup,
    sampler: Sampler,

    // SoA + params
    spheres_buf: Buffer,
    albedos_buf: Buffer,
    params_buf: Buffer,

    // camera
    camera_pos: [f32; 3],
    yaw: f32,
    pitch: f32,
    move_speed: f32,
    mouse_sensitivity: f32,
}

impl Gfx {
    pub fn new(event_loop: &ActiveEventLoop) -> Result<Self> {
        let mut attrs = Window::default_attributes()
            .with_title("callandor")
            .with_inner_size(PhysicalSize::new(1920, 1080));
        #[cfg(target_os = "macos")]
        {
            use winit::platform::macos::WindowAttributesExtMacOS;
            attrs = attrs.with_disallow_hidpi(true);
        }

        let window = Arc::new(event_loop.create_window(attrs)?);

        let instance = Instance::new(&InstanceDescriptor {
            backends: Backends::all(),
            ..Default::default()
        });
        let surface = instance.create_surface(window.clone())?;

        let adapter = instance
            .request_adapter(&RequestAdapterOptions {
                power_preference: PowerPreference::HighPerformance,
                force_fallback_adapter: false,
                compatible_surface: Some(&surface),
            })
            .block_on()
            .expect("No suitable GPU adapters found");

        let device_desc = DeviceDescriptor {
            label: None,
            required_features: Features::empty(),
            required_limits: Limits::default(),
            experimental_features: ExperimentalFeatures::disabled(),
            memory_hints: MemoryHints::Performance,
            trace: Trace::default(),
        };
        let (device, queue) = adapter.request_device(&device_desc).block_on()?;

        let size = window.inner_size();
        let caps = surface.get_capabilities(&adapter);
        let surface_format = caps
            .formats
            .iter()
            .copied()
            .find(|f| {
                matches!(
                    f,
                    TextureFormat::Bgra8UnormSrgb | TextureFormat::Rgba8UnormSrgb
                )
            })
            .unwrap_or(caps.formats[0]);
        let surface_cfg = SurfaceConfiguration {
            usage: TextureUsages::RENDER_ATTACHMENT,
            format: surface_format,
            width: size.width.max(1),
            height: size.height.max(1),
            present_mode: PresentMode::Fifo,
            alpha_mode: caps.alpha_modes[0],
            view_formats: vec![],
            desired_maximum_frame_latency: 2,
        };
        surface.configure(&device, &surface_cfg);

        // Shaders
        let cs = device.create_shader_module(include_wgsl!("../shaders/compute.wgsl"));
        let fsq = device.create_shader_module(include_wgsl!("../shaders/fullscreen.wgsl"));

        // Storage image
        let (storage_tex, storage_view) =
            Self::make_storage(&device, surface_cfg.width, surface_cfg.height);

        // SoA buffers (filled on GPU by gen_main)
        let spheres_buf = device.create_buffer(&BufferDescriptor {
            label: Some("spheres (cx,cy,cz,r)"),
            size: (SPHERE_COUNT as u64) * 16, // vec4<f32>
            usage: BufferUsages::STORAGE,
            mapped_at_creation: false,
        });
        let albedos_buf = device.create_buffer(&BufferDescriptor {
            label: Some("albedos (r,g,b,_)"),
            size: (SPHERE_COUNT as u64) * 16, // vec4<f32>
            usage: BufferUsages::STORAGE,
            mapped_at_creation: false,
        });
        let params_buf = device.create_buffer(&BufferDescriptor {
            label: Some("params uniform"),
            size: std::mem::size_of::<Params>() as u64,
            usage: BufferUsages::UNIFORM | BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });

        // Compute bind group layout: storage image + SoA buffers + uniform
        let compute_bgl = device.create_bind_group_layout(&BindGroupLayoutDescriptor {
            label: Some("compute layout"),
            entries: &[
                // out_img
                BindGroupLayoutEntry {
                    binding: 0,
                    visibility: ShaderStages::COMPUTE,
                    ty: BindingType::StorageTexture {
                        access: StorageTextureAccess::WriteOnly,
                        format: TextureFormat::Rgba8Unorm,
                        view_dimension: TextureViewDimension::D2,
                    },
                    count: None,
                },
                // spheres
                BindGroupLayoutEntry {
                    binding: 1,
                    visibility: ShaderStages::COMPUTE,
                    ty: BindingType::Buffer {
                        ty: BufferBindingType::Storage { read_only: false },
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
                // albedos
                BindGroupLayoutEntry {
                    binding: 2,
                    visibility: ShaderStages::COMPUTE,
                    ty: BindingType::Buffer {
                        ty: BufferBindingType::Storage { read_only: false },
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
                // params
                BindGroupLayoutEntry {
                    binding: 3,
                    visibility: ShaderStages::COMPUTE,
                    ty: BindingType::Buffer {
                        ty: BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
            ],
        });
        let compute_layout = device.create_pipeline_layout(&PipelineLayoutDescriptor {
            label: Some("compute pipeline layout"),
            bind_group_layouts: &[&compute_bgl],
            push_constant_ranges: &[],
        });

        // Two compute pipelines: generator + tracer
        let gen_pip = device.create_compute_pipeline(&ComputePipelineDescriptor {
            label: Some("gen spheres pipeline"),
            layout: Some(&compute_layout),
            module: &cs,
            entry_point: Some("gen_main"),
            compilation_options: PipelineCompilationOptions::default(),
            cache: None,
        });
        let trace_pip = device.create_compute_pipeline(&ComputePipelineDescriptor {
            label: Some("trace pipeline"),
            layout: Some(&compute_layout),
            module: &cs,
            entry_point: Some("trace_main"),
            compilation_options: PipelineCompilationOptions::default(),
            cache: None,
        });

        let compute_bg = device.create_bind_group(&BindGroupDescriptor {
            label: Some("compute bind group"),
            layout: &compute_bgl,
            entries: &[
                BindGroupEntry {
                    binding: 0,
                    resource: BindingResource::TextureView(&storage_view),
                },
                BindGroupEntry {
                    binding: 1,
                    resource: spheres_buf.as_entire_binding(),
                },
                BindGroupEntry {
                    binding: 2,
                    resource: albedos_buf.as_entire_binding(),
                },
                BindGroupEntry {
                    binding: 3,
                    resource: params_buf.as_entire_binding(),
                },
            ],
        });

        // Fullscreen sampling of the storage image
        let render_bgl = device.create_bind_group_layout(&BindGroupLayoutDescriptor {
            label: Some("render layout"),
            entries: &[
                BindGroupLayoutEntry {
                    binding: 0,
                    visibility: ShaderStages::FRAGMENT,
                    ty: BindingType::Texture {
                        multisampled: false,
                        view_dimension: TextureViewDimension::D2,
                        sample_type: TextureSampleType::Float { filterable: true },
                    },
                    count: None,
                },
                BindGroupLayoutEntry {
                    binding: 1,
                    visibility: ShaderStages::FRAGMENT,
                    ty: BindingType::Sampler(SamplerBindingType::Filtering),
                    count: None,
                },
            ],
        });
        let sampler = device.create_sampler(&SamplerDescriptor {
            label: Some("render sampler"),
            address_mode_u: wgpu::AddressMode::ClampToEdge,
            address_mode_v: wgpu::AddressMode::ClampToEdge,
            address_mode_w: wgpu::AddressMode::ClampToEdge,
            ..Default::default()
        });
        let render_layout = device.create_pipeline_layout(&PipelineLayoutDescriptor {
            label: Some("render pipeline layout"),
            bind_group_layouts: &[&render_bgl],
            push_constant_ranges: &[],
        });
        let render_pip = device.create_render_pipeline(&RenderPipelineDescriptor {
            label: Some("render pipeline"),
            layout: Some(&render_layout),
            vertex: VertexState {
                module: &fsq,
                entry_point: Some("vs_main"),
                buffers: &[],
                compilation_options: Default::default(),
            },
            fragment: Some(FragmentState {
                module: &fsq,
                entry_point: Some("fs_main"),
                targets: &[Some(ColorTargetState {
                    format: surface_format,
                    blend: None,
                    write_mask: ColorWrites::ALL,
                })],
                compilation_options: Default::default(),
            }),
            primitive: PrimitiveState::default(),
            depth_stencil: None,
            multisample: MultisampleState::default(),
            multiview: None,
            cache: None,
        });
        let render_bg = device.create_bind_group(&BindGroupDescriptor {
            label: Some("render bind group"),
            layout: &render_bgl,
            entries: &[
                BindGroupEntry {
                    binding: 0,
                    resource: BindingResource::TextureView(&storage_view),
                },
                BindGroupEntry {
                    binding: 1,
                    resource: BindingResource::Sampler(&sampler),
                },
            ],
        });

        let camera_pos = [0.0f32, 2.0, 8.0];
        let initial_forward = normalize([0.0, -0.2, -1.0]);
        let yaw = initial_forward[2].atan2(initial_forward[0]);
        let pitch = initial_forward[1].asin();

        Ok(Self {
            window,
            _instance: instance,
            surface,
            device,
            queue,
            surface_cfg,
            _surface_format: surface_format,
            _storage_tex: storage_tex,
            storage_view,
            gen_pip,
            trace_pip,
            compute_bgl,
            compute_bg,
            render_pip,
            render_bgl,
            render_bg,
            sampler,
            spheres_buf,
            albedos_buf,
            params_buf,
            camera_pos,
            yaw,
            pitch,
            move_speed: 6.0,
            mouse_sensitivity: 0.0025,
        })
    }

    pub fn update_camera(&mut self, dt: f32, input: CameraInput) {
        if !dt.is_finite() || dt <= 0.0 {
            return;
        }

        let (forward, right, up) = self.camera_vectors();
        let mut movement = [0.0f32, 0.0, 0.0];
        if input.forward.abs() > f32::EPSILON {
            movement = vec_add(movement, vec_scale(forward, input.forward));
        }
        if input.strafe.abs() > f32::EPSILON {
            movement = vec_add(movement, vec_scale(right, input.strafe));
        }
        if input.ascend.abs() > f32::EPSILON {
            movement = vec_add(movement, vec_scale(up, input.ascend));
        }

        let len = vec_length(movement);
        if len <= f32::EPSILON {
            return;
        }

        let move_dir = vec_scale(movement, 1.0 / len);
        let speed = if input.boost {
            self.move_speed * 3.0
        } else {
            self.move_speed
        };
        self.camera_pos = vec_add(self.camera_pos, vec_scale(move_dir, speed * dt));
    }

    pub fn look_delta(&mut self, delta: (f32, f32)) {
        let (dx, dy) = delta;
        self.yaw = (self.yaw + dx * self.mouse_sensitivity).rem_euclid(TAU);
        if self.yaw > PI {
            self.yaw -= TAU;
        }

        self.pitch -= dy * self.mouse_sensitivity;
        let clamp = FRAC_PI_2 - 0.01;
        self.pitch = self.pitch.clamp(-clamp, clamp);
    }

    fn camera_vectors(&self) -> ([f32; 3], [f32; 3], [f32; 3]) {
        let cos_pitch = self.pitch.cos();
        let forward = normalize([
            cos_pitch * self.yaw.cos(),
            self.pitch.sin(),
            cos_pitch * self.yaw.sin(),
        ]);

        let world_up = [0.0f32, 1.0, 0.0];
        let mut right = cross(forward, world_up);
        if vec_length(right) < 1e-4 {
            right = [1.0, 0.0, 0.0];
        }
        right = normalize(right);
        let up = normalize(cross(right, forward));
        (forward, right, up)
    }

    pub fn window(&self) -> &Window {
        &self.window
    }

    fn make_storage(device: &Device, width: u32, height: u32) -> (Texture, TextureView) {
        let tex = device.create_texture(&TextureDescriptor {
            label: Some("storage texture"),
            size: Extent3d {
                width: width.max(1),
                height: height.max(1),
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: TextureDimension::D2,
            format: TextureFormat::Rgba8Unorm,
            usage: TextureUsages::STORAGE_BINDING | TextureUsages::TEXTURE_BINDING,
            view_formats: &[],
        });
        let view = tex.create_view(&TextureViewDescriptor::default());
        (tex, view)
    }

    pub fn resize(&mut self, new_size: winit::dpi::PhysicalSize<u32>) {
        if new_size.width == 0 || new_size.height == 0 {
            return;
        }
        self.surface_cfg.width = new_size.width;
        self.surface_cfg.height = new_size.height;
        self.surface.configure(&self.device, &self.surface_cfg);

        let (tex, view) = Self::make_storage(&self.device, new_size.width, new_size.height);
        self._storage_tex = tex;
        self.storage_view = view;

        // Re-bind (note: buffers/params unchanged)
        self.compute_bg = self.device.create_bind_group(&BindGroupDescriptor {
            label: Some("compute bind group"),
            layout: &self.compute_bgl,
            entries: &[
                BindGroupEntry {
                    binding: 0,
                    resource: BindingResource::TextureView(&self.storage_view),
                },
                BindGroupEntry {
                    binding: 1,
                    resource: self.spheres_buf.as_entire_binding(),
                },
                BindGroupEntry {
                    binding: 2,
                    resource: self.albedos_buf.as_entire_binding(),
                },
                BindGroupEntry {
                    binding: 3,
                    resource: self.params_buf.as_entire_binding(),
                },
            ],
        });

        self.render_bg = self.device.create_bind_group(&BindGroupDescriptor {
            label: Some("render bind group"),
            layout: &self.render_bgl,
            entries: &[
                BindGroupEntry {
                    binding: 0,
                    resource: BindingResource::TextureView(&self.storage_view),
                },
                BindGroupEntry {
                    binding: 1,
                    resource: BindingResource::Sampler(&self.sampler),
                },
            ],
        });
    }

    pub fn render(&mut self) -> Result<()> {
        // Update camera params every frame.
        let aspect = self.surface_cfg.width as f32 / self.surface_cfg.height as f32;
        let (forward, right, up) = self.camera_vectors();

        let fov_y = 60.0f32.to_radians();
        let half_fov_tan = (0.5 * fov_y).tan();
        let ground_y = 0.0f32;

        let params = Params {
            cam_pos: self.camera_pos,
            sphere_count: SPHERE_COUNT,
            cam_fwd: forward,
            _pad0: 0,
            cam_right: right,
            _pad1: 0,
            cam_up: up,
            _pad2: 0,
            half_fov_tan,
            aspect,
            ground_y,
            ambient: 0.05,
            // Direction of light rays (from light toward scene); shader negates for shading.
            light_dir: normalize([0.4, -1.0, 0.6]),
            _pad3: 0,
            light_color: [1.0, 1.0, 1.0],
            _pad4: 0,
        };
        self.queue
            .write_buffer(&self.params_buf, 0, bytemuck::bytes_of(&params));

        // Frame
        let output = match self.surface.get_current_texture() {
            Ok(frame) => frame,
            Err(SurfaceError::Outdated | SurfaceError::Lost) => {
                self.surface.configure(&self.device, &self.surface_cfg);
                return Ok(());
            }
            Err(SurfaceError::OutOfMemory) => bail!("surface out of memory"),
            Err(SurfaceError::Other) => bail!("surface acquisition failed"),
            Err(SurfaceError::Timeout) => return Ok(()),
        };
        let surface_view = output
            .texture
            .create_view(&TextureViewDescriptor::default());

        let mut encoder = self
            .device
            .create_command_encoder(&CommandEncoderDescriptor {
                label: Some("encoder"),
            });

        // Compute: 1) generate spheres (O(N)), 2) trace (O(W*H*N))
        {
            let mut pass = encoder.begin_compute_pass(&ComputePassDescriptor {
                label: Some("compute pass"),
                timestamp_writes: None,
            });
            pass.set_bind_group(0, &self.compute_bg, &[]);

            // 1) generate SoA data
            let gx = ceil_div(SPHERE_COUNT, 64);
            pass.set_pipeline(&self.gen_pip);
            pass.dispatch_workgroups(gx, 1, 1);

            // 2) trace
            let [w, h] = [self.surface_cfg.width, self.surface_cfg.height];
            let tx = ceil_div(w, 16);
            let ty = ceil_div(h, 16);
            pass.set_pipeline(&self.trace_pip);
            pass.dispatch_workgroups(tx, ty, 1);
        }

        // Blit to screen
        {
            let mut pass = encoder.begin_render_pass(&RenderPassDescriptor {
                label: Some("render pass"),
                color_attachments: &[Some(RenderPassColorAttachment {
                    view: &surface_view,
                    depth_slice: None,
                    resolve_target: None,
                    ops: Operations {
                        load: LoadOp::Clear(Color::BLACK),
                        store: StoreOp::Store,
                    },
                })],
                depth_stencil_attachment: None,
                occlusion_query_set: None,
                timestamp_writes: None,
            });
            pass.set_pipeline(&self.render_pip);
            pass.set_bind_group(0, &self.render_bg, &[]);
            pass.draw(0..3, 0..1);
        }

        self.queue.submit([encoder.finish()]);
        output.present();
        Ok(())
    }
}

fn vec_add(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
}

fn vec_scale(v: [f32; 3], s: f32) -> [f32; 3] {
    [v[0] * s, v[1] * s, v[2] * s]
}

fn vec_length(v: [f32; 3]) -> f32 {
    (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt()
}

fn normalize(v: [f32; 3]) -> [f32; 3] {
    let len = vec_length(v);
    if len > f32::EPSILON {
        vec_scale(v, 1.0 / len)
    } else {
        v
    }
}

fn cross(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}
