use std::sync::Arc;

use anyhow::anyhow;
use vulkano::{
    Validated, VulkanError, VulkanLibrary,
    command_buffer::{
        AutoCommandBufferBuilder, BlitImageInfo, CommandBufferUsage, CopyImageInfo,
        allocator::{StandardCommandBufferAllocator, StandardCommandBufferAllocatorCreateInfo},
    },
    descriptor_set::{
        DescriptorSet, WriteDescriptorSet,
        allocator::{StandardDescriptorSetAllocator, StandardDescriptorSetAllocatorCreateInfo},
    },
    device::{
        Device, DeviceCreateInfo, DeviceExtensions, Queue, QueueCreateInfo, QueueFlags,
        physical::PhysicalDevice,
    },
    format::Format,
    image::{Image, ImageCreateInfo, ImageLayout, ImageType, ImageUsage, view::ImageView},
    instance::{Instance, InstanceCreateFlags, InstanceCreateInfo},
    memory::allocator::{AllocationCreateInfo, MemoryTypeFilter, StandardMemoryAllocator},
    pipeline::{
        Pipeline, PipelineBindPoint, PipelineShaderStageCreateInfo,
        compute::{ComputePipeline, ComputePipelineCreateInfo},
        layout::{PipelineDescriptorSetLayoutCreateInfo, PipelineLayout},
    },
    swapchain::{self, PresentMode, Surface, Swapchain, SwapchainCreateInfo, SwapchainPresentInfo},
    sync::{self, GpuFuture},
};
use winit::{
    application::ApplicationHandler,
    event::WindowEvent,
    event_loop::{ActiveEventLoop, ControlFlow, EventLoop},
    window::{Window, WindowId},
};

type AnyError = anyhow::Error;
type Result<T = ()> = std::result::Result<T, AnyError>;

#[inline]
fn ceil_div(x: u32, d: u32) -> u32 {
    (x + d - 1) / d
}

const STORAGE_FORMAT: Format = Format::R8G8B8A8_UNORM;

mod gradient_shader {
    vulkano_shaders::shader! {
        ty: "compute",
        path: "shaders/gradient.comp",
    }
}

struct Gfx {
    _instance: Arc<Instance>,
    device: Arc<Device>,
    queue: Arc<Queue>,

    window: Arc<Window>,
    _surface: Arc<Surface>,
    swapchain: Arc<Swapchain>,
    swapchain_images: Vec<Arc<Image>>,
    swapchain_format: Format,

    mem_alloc: Arc<StandardMemoryAllocator>,
    set_alloc: Arc<StandardDescriptorSetAllocator>,
    cmd_alloc: Arc<StandardCommandBufferAllocator>,

    pipeline: Arc<ComputePipeline>,
    storage_image: Arc<Image>,
    storage_set: Arc<DescriptorSet>,

    recreate_swapchain: bool,
}

impl Gfx {
    fn new(event_loop: &ActiveEventLoop) -> Result<Self> {
        let library = VulkanLibrary::new()?;
        let instance_ext = Surface::required_extensions(event_loop)?;
        // On portability stacks, you must enumerate non-conformant devices (MoltenVK guidance).
        let instance = Instance::new(
            library.clone(),
            InstanceCreateInfo {
                enabled_extensions: instance_ext,
                enabled_layers: {
                    let mut layers = Vec::<String>::new();
                    if cfg!(debug_assertions) {
                        if library
                            .layer_properties()?
                            .any(|l| l.name() == "VK_LAYER_KHRONOS_validation")
                        {
                            layers.push("VK_LAYER_KHRONOS_validation".into());
                        }
                    }
                    layers
                },
                flags: InstanceCreateFlags::ENUMERATE_PORTABILITY,
                ..Default::default()
            },
        )?;

        let window = Arc::new(
            event_loop.create_window(Window::default_attributes().with_title("callandor"))?,
        );
        let surface = Surface::from_window(instance.clone(), window.clone())?;

        let (physical, queue_family_index) = pick_device(&instance, &surface)?;
        let mut dev_ext = DeviceExtensions {
            khr_swapchain: true,
            ..DeviceExtensions::empty()
        };
        let supported = physical.supported_extensions();
        if supported.khr_portability_subset {
            dev_ext.khr_portability_subset = true;
        }
        let (device, mut queues) = Device::new(
            physical.clone(),
            DeviceCreateInfo {
                enabled_extensions: dev_ext,
                queue_create_infos: vec![QueueCreateInfo {
                    queue_family_index,
                    ..Default::default()
                }],
                ..Default::default()
            },
        )?;
        let queue = queues.next().unwrap();

        let mem_alloc = Arc::new(StandardMemoryAllocator::new_default(device.clone()));
        let set_alloc = Arc::new(StandardDescriptorSetAllocator::new(
            device.clone(),
            StandardDescriptorSetAllocatorCreateInfo::default(),
        ));
        let cmd_alloc = Arc::new(StandardCommandBufferAllocator::new(
            device.clone(),
            StandardCommandBufferAllocatorCreateInfo::default(),
        ));

        let (swapchain, swapchain_images, swapchain_format) =
            create_swapchain(&physical, &device, &surface, &window, queue_family_index)?;

        let pipeline = create_compute_pipeline(device.clone())?;

        let (storage_image, storage_set) =
            create_storage_bindings(&mem_alloc, &set_alloc, &pipeline, swapchain.image_extent())?;

        Ok(Self {
            _instance: instance,
            device,
            queue,
            window,
            _surface: surface,
            swapchain,
            swapchain_images,
            swapchain_format,
            mem_alloc,
            set_alloc,
            cmd_alloc,
            pipeline,
            storage_image,
            storage_set,
            recreate_swapchain: false,
        })
    }

    fn ensure_swapchain_current(&mut self) -> Result {
        if !self.recreate_swapchain {
            return Ok(());
        }
        self.recreate_swapchain = false;

        let extent: [u32; 2] = self.window.inner_size().into();

        let (swapchain, images) = self.swapchain.recreate(SwapchainCreateInfo {
            image_extent: extent,
            ..self.swapchain.create_info()
        })?;

        self.swapchain = swapchain;
        self.swapchain_images = images;

        let (img, set) =
            create_storage_bindings(&self.mem_alloc, &self.set_alloc, &self.pipeline, extent)?;
        self.storage_image = img;
        self.storage_set = set;

        Ok(())
    }

    fn render(&mut self) -> Result {
        self.ensure_swapchain_current()?;

        let (image_i, suboptimal, acquire) =
            match swapchain::acquire_next_image(self.swapchain.clone(), None)
                .map_err(Validated::unwrap)
            {
                Ok(v) => v,
                Err(VulkanError::OutOfDate) => {
                    self.recreate_swapchain = true;
                    return Ok(());
                }
                Err(e) => return Err(anyhow!("acquire_next_image: {e}")),
            };
        if suboptimal {
            self.recreate_swapchain = true;
        }

        let [w, h, _] = self.storage_image.extent();
        let workgroups = [ceil_div(w, 16), ceil_div(h, 16), 1];

        let mut builder = AutoCommandBufferBuilder::primary(
            self.cmd_alloc.clone(),
            self.queue.queue_family_index(),
            CommandBufferUsage::OneTimeSubmit,
        )?;

        builder
            .bind_pipeline_compute(self.pipeline.clone())?
            .bind_descriptor_sets(
                PipelineBindPoint::Compute,
                self.pipeline.layout().clone(),
                0,
                self.storage_set.clone(),
            )?;
        unsafe { builder.dispatch(workgroups)?; }

        let dst = self.swapchain_images[image_i as usize].clone();
        if self.storage_image.format() == self.swapchain_format {
            builder.copy_image(CopyImageInfo::images(self.storage_image.clone(), dst))?;
        } else {
            let mut blit = BlitImageInfo::images(self.storage_image.clone(), dst);
            blit.src_image_layout = ImageLayout::General;
            builder.blit_image(blit)?;
        }

        let cb = builder.build()?;

        let presented = sync::now(self.device.clone())
            .join(acquire)
            .then_execute(self.queue.clone(), cb)?
            .then_swapchain_present(
                self.queue.clone(),
                SwapchainPresentInfo::swapchain_image_index(self.swapchain.clone(), image_i),
            )
            .then_signal_fence_and_flush();

        match presented.map_err(Validated::unwrap) {
            Ok(f) => {
                let _ = f.wait(None);
            }
            Err(VulkanError::OutOfDate) => self.recreate_swapchain = true,
            Err(e) => eprintln!("present error: {e}"),
        }

        Ok(())
    }
}

struct App {
    gfx: Option<Gfx>,
}

impl App {
    fn new() -> Self {
        Self { gfx: None }
    }
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.gfx.is_none() {
            self.gfx = Some(Gfx::new(event_loop).unwrap());
        }
        event_loop.set_control_flow(ControlFlow::Poll);
    }

    fn about_to_wait(&mut self, _event_loop: &ActiveEventLoop) {
        if let Some(gfx) = &self.gfx {
            gfx.window.request_redraw();
        }
    }

    fn window_event(
        &mut self,
        _event_loop: &ActiveEventLoop,
        _window_id: WindowId,
        event: WindowEvent,
    ) {
        let Some(gfx) = self.gfx.as_mut() else {
            return;
        };
        match event {
            WindowEvent::CloseRequested => std::process::exit(0),
            WindowEvent::Resized(_) => gfx.recreate_swapchain = true,
            WindowEvent::RedrawRequested => {
                if let Err(e) = gfx.render() {
                    eprintln!("render error: {e}");
                }
            }
            _ => {}
        }
    }
}

fn main() -> Result {
    let event_loop = EventLoop::new()?;
    let mut app = App::new();
    event_loop.run_app(&mut app)?;
    Ok(())
}

fn pick_device(instance: &Arc<Instance>, surface: &Arc<Surface>) -> Result<(Arc<PhysicalDevice>, u32)> {
    for physical in instance.enumerate_physical_devices()? {
        if let Some(i) = first_usable_queue_family(&physical, surface)? {
            return Ok((physical, i));
        }
    }
    Err(anyhow!("no suitable physical device"))
}

fn first_usable_queue_family(p: &PhysicalDevice, surface: &Arc<Surface>) -> Result<Option<u32>> {
    for (idx, fam) in p.queue_family_properties().iter().enumerate() {
        let i = idx as u32;
        if !fam.queue_flags.contains(QueueFlags::GRAPHICS | QueueFlags::COMPUTE) {
            continue;
        }
        if !p.surface_support(i, surface)? {
            continue;
        }
        if p.surface_formats(surface, Default::default())?.is_empty() {
            continue;
        }
        if p.surface_present_modes(surface, Default::default())?.is_empty() {
            continue;
        }
        return Ok(Some(i));
    }
    Ok(None)
}

fn create_swapchain(
    physical: &Arc<PhysicalDevice>,
    device: &Arc<Device>,
    surface: &Arc<Surface>,
    window: &Arc<Window>,
    _queue_family_index: u32,
) -> Result<(Arc<Swapchain>, Vec<Arc<Image>>, Format)> {
    let formats = physical.surface_formats(surface, Default::default())?;
    let (format, _colorspace) = formats
        .iter()
        .copied()
        .find(|(f, _)| {
            matches!(
                f,
                Format::B8G8R8A8_UNORM
                    | Format::B8G8R8A8_SRGB
                    | Format::R8G8B8A8_UNORM
                    | Format::R8G8B8A8_SRGB
            )
        })
        .unwrap_or(formats[0]);

    let image_extent: [u32; 2] = window.inner_size().into();

    let (swapchain, images) = Swapchain::new(
        device.clone(),
        surface.clone(),
        SwapchainCreateInfo {
            min_image_count: 3,
            image_format: format,
            image_extent,
            image_usage: ImageUsage::TRANSFER_DST,
            present_mode: PresentMode::Fifo,
            ..Default::default()
        },
    )?;

    Ok((swapchain, images, format))
}

fn create_compute_pipeline(device: Arc<Device>) -> Result<Arc<ComputePipeline>> {
    let shader = gradient_shader::load(device.clone())?;

    let cs_entry = shader.entry_point("main").unwrap();
    let stage = PipelineShaderStageCreateInfo::new(cs_entry);
    let layout = PipelineLayout::new(
        device.clone(),
        PipelineDescriptorSetLayoutCreateInfo::from_stages([&stage])
            .into_pipeline_layout_create_info(device.clone())?,
    )?;

    Ok(ComputePipeline::new(
        device,
        None,
        ComputePipelineCreateInfo::stage_layout(stage, layout),
    )?)
}

fn create_storage_bindings(
    mem_alloc: &Arc<StandardMemoryAllocator>,
    set_alloc: &Arc<StandardDescriptorSetAllocator>,
    pipeline: &Arc<ComputePipeline>,
    extent: [u32; 2],
) -> Result<(Arc<Image>, Arc<DescriptorSet>)> {
    let storage_image = Image::new(
        mem_alloc.clone(),
        ImageCreateInfo {
            image_type: ImageType::Dim2d,
            format: STORAGE_FORMAT,
            extent: [extent[0], extent[1], 1],
            usage: ImageUsage::STORAGE | ImageUsage::TRANSFER_SRC,
            ..Default::default()
        },
        AllocationCreateInfo {
            memory_type_filter: MemoryTypeFilter::PREFER_DEVICE,
            ..Default::default()
        },
    )?;

    let layout0 = pipeline.layout().set_layouts().get(0).unwrap().clone();
    let set = DescriptorSet::new(
        set_alloc.clone(),
        layout0,
        [WriteDescriptorSet::image_view(
            0,
            ImageView::new_default(storage_image.clone())?,
        )],
        [],
    )?;

    Ok((storage_image, set))
}
