use std::{error::Error, sync::Arc};
use vulkano::{
    VulkanLibrary,
    device::{
        Device, DeviceCreateInfo, DeviceExtensions, Queue, QueueCreateInfo, QueueFlags,
        physical::PhysicalDevice,
    },
    instance::{Instance, InstanceCreateFlags, InstanceCreateInfo},
    swapchain::Surface,
};
use winit::{
    application::ApplicationHandler,
    event::WindowEvent,
    event_loop::{ActiveEventLoop, ControlFlow, EventLoop},
    window::{Window, WindowId},
};

type AnyError = Box<dyn Error + Send + Sync>;

struct App {
    instance: Arc<Instance>,
    window: Option<Arc<Window>>,
    surface: Option<Arc<Surface>>,
    device: Option<Arc<Device>>,
    graphics_queue: Option<Arc<Queue>>,
    present_queue: Option<Arc<Queue>>,
}

impl App {
    fn new(instance: Arc<Instance>) -> Self {
        Self {
            instance,
            window: None,
            surface: None,
            device: None,
            graphics_queue: None,
            present_queue: None,
        }
    }

    fn init(&mut self, event_loop: &ActiveEventLoop) -> Result<(), AnyError> {
        let (window, surface) = create_window_and_surface(self.instance.clone(), event_loop)?;
        let (physical_device, graphics_family, present_family) =
            pick_device_and_families(&self.instance, &surface)?;
        let (device, graphics_queue, present_queue) =
            create_device_and_queues(physical_device, graphics_family, present_family)?;

        self.window = Some(window);
        self.surface = Some(surface);
        self.device = Some(device);
        self.graphics_queue = Some(graphics_queue);
        self.present_queue = Some(present_queue);

        Ok(())
    }
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.window.is_none() {
            if let Err(err) = self.init(event_loop) {
                eprintln!("failed to initialize Vulkan: {err}");
                std::process::exit(1);
            }
        }
        event_loop.set_control_flow(ControlFlow::Poll);
    }

    fn window_event(
        &mut self,
        _event_loop: &ActiveEventLoop,
        _window_id: WindowId,
        event: WindowEvent,
    ) {
        if matches!(event, WindowEvent::CloseRequested) {
            std::process::exit(0);
        }
    }
}

fn main() -> Result<(), AnyError> {
    let event_loop = EventLoop::new()?;
    let instance = create_instance(&event_loop)?;
    let mut app = App::new(instance);
    event_loop.run_app(&mut app)?;
    Ok(())
}

fn create_instance(event_loop: &EventLoop<()>) -> Result<Arc<Instance>, AnyError> {
    let library = VulkanLibrary::new()?;
    let extensions = Surface::required_extensions(event_loop)?;

    let mut enabled_layers = Vec::<String>::new();
    if cfg!(debug_assertions) {
        if library
            .layer_properties()?
            .any(|layer| layer.name() == "VK_LAYER_KHRONOS_validation")
        {
            enabled_layers.push("VK_LAYER_KHRONOS_validation".into());
        }
    }

    let instance = Instance::new(
        library,
        InstanceCreateInfo {
            enabled_extensions: extensions,
            enabled_layers,
            flags: InstanceCreateFlags::ENUMERATE_PORTABILITY,
            ..Default::default()
        },
    )?;

    Ok(instance)
}

fn create_window_and_surface(
    instance: Arc<Instance>,
    event_loop: &ActiveEventLoop,
) -> Result<(Arc<Window>, Arc<Surface>), AnyError> {
    let attributes = Window::default_attributes().with_title("callandor");
    let window = Arc::new(event_loop.create_window(attributes)?);
    let surface = Surface::from_window(instance, window.clone())?;
    Ok((window, surface))
}

fn pick_device_and_families(
    instance: &Arc<Instance>,
    surface: &Arc<Surface>,
) -> Result<(Arc<PhysicalDevice>, u32, u32), AnyError> {
    for physical in instance.enumerate_physical_devices()? {
        let mut graphics = None;
        let mut present = None;

        for (index, family) in physical.queue_family_properties().iter().enumerate() {
            let idx = index as u32;
            if family.queue_flags.contains(QueueFlags::GRAPHICS) {
                graphics.get_or_insert(idx);
            }
            if physical.surface_support(idx, surface)? {
                present.get_or_insert(idx);
            }
            if graphics.is_some() && present.is_some() {
                break;
            }
        }

        if let (Some(graphics_family), Some(present_family)) = (graphics, present) {
            let formats = physical.surface_formats(surface, Default::default())?;
            let present_modes = physical.surface_present_modes(surface, Default::default())?;

            if !formats.is_empty() && !present_modes.is_empty() {
                return Ok((physical, graphics_family, present_family));
            }
        }
    }

    Err("no suitable physical device found".into())
}

fn create_device_and_queues(
    physical_device: Arc<PhysicalDevice>,
    graphics_family: u32,
    present_family: u32,
) -> Result<(Arc<Device>, Arc<Queue>, Arc<Queue>), AnyError> {
    let mut queues = vec![QueueCreateInfo {
        queue_family_index: graphics_family,
        ..Default::default()
    }];

    if present_family != graphics_family {
        queues.push(QueueCreateInfo {
            queue_family_index: present_family,
            ..Default::default()
        });
    }

    let (device, mut queue_iter) = Device::new(
        physical_device,
        DeviceCreateInfo {
            enabled_extensions: DeviceExtensions {
                khr_swapchain: true,
                ..DeviceExtensions::empty()
            },
            queue_create_infos: queues,
            ..Default::default()
        },
    )?;

    let graphics_queue = queue_iter.next().expect("graphics queue should exist");
    let present_queue = if present_family != graphics_family {
        queue_iter.next().expect("present queue should exist")
    } else {
        graphics_queue.clone()
    };

    Ok((device, graphics_queue, present_queue))
}
