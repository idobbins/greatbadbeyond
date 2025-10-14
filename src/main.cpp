#include <vulkan/vulkan.h>

#if defined(__APPLE__)
#include <vulkan/vulkan_metal.h>
#include <vulkan/vulkan_beta.h>
#endif

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <random>
#include <string_view>
#include <vector>
#include <cmath>
#include <limits>
#include <bit>

using namespace std;

using  u8  = uint8_t;
using  u16 = uint16_t;
using  u32 = uint32_t;
using  u64 = uint64_t;

using  i8  = int8_t;
using  i16 = int16_t;
using  i32 = int32_t;
using  i64 = int64_t;

using  f32 = float;
using  f64 = double;

using  usize = size_t;
using  isize = ptrdiff_t;

constexpr f32 PI = 3.14159265358979323846f;

struct Vec3 {
  f32 x = 0.0f;
  f32 y = 0.0f;
  f32 z = 0.0f;
};

inline Vec3 operator+(const Vec3 a, const Vec3 b) {
  return Vec3{ a.x + b.x, a.y + b.y, a.z + b.z };
}

inline Vec3 operator-(const Vec3 a, const Vec3 b) {
  return Vec3{ a.x - b.x, a.y - b.y, a.z - b.z };
}

inline Vec3 operator-(const Vec3 v) {
  return Vec3{ -v.x, -v.y, -v.z };
}

inline Vec3 operator*(const Vec3 v, const f32 s) {
  return Vec3{ v.x * s, v.y * s, v.z * s };
}

inline Vec3 operator*(const f32 s, const Vec3 v) {
  return v * s;
}

inline Vec3 operator/(const Vec3 v, const f32 s) {
  return Vec3{ v.x / s, v.y / s, v.z / s };
}

inline Vec3& operator+=(Vec3& a, const Vec3 b) {
  a = a + b;
  return a;
}

inline Vec3& operator-=(Vec3& a, const Vec3 b) {
  a = a - b;
  return a;
}

inline f32 dot(const Vec3 a, const Vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3 a, const Vec3 b) {
  return Vec3{
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x,
  };
}

inline f32 length_squared(const Vec3 v) {
  return dot(v, v);
}

inline f32 length(const Vec3 v) {
  return std::sqrt(length_squared(v));
}

inline Vec3 normalize(const Vec3 v) {
  const f32 len = length(v);
  if (len <= 0.0f) {
    return Vec3{ 0.0f, 0.0f, 0.0f };
  }
  return v / len;
}

inline Vec3 clamp_vec3(const Vec3 v, const f32 min_value, const f32 max_value) {
  return Vec3{
    std::clamp(v.x, min_value, max_value),
    std::clamp(v.y, min_value, max_value),
    std::clamp(v.z, min_value, max_value),
  };
}

inline Vec3 clamp_vec3(const Vec3 v, const Vec3 min_values, const Vec3 max_values) {
  return Vec3{
    std::clamp(v.x, min_values.x, max_values.x),
    std::clamp(v.y, min_values.y, max_values.y),
    std::clamp(v.z, min_values.z, max_values.z),
  };
}

inline Vec3 min_vec3(const Vec3 a, const Vec3 b) {
  return Vec3{
    std::min(a.x, b.x),
    std::min(a.y, b.y),
    std::min(a.z, b.z),
  };
}

inline Vec3 max_vec3(const Vec3 a, const Vec3 b) {
  return Vec3{
    std::max(a.x, b.x),
    std::max(a.y, b.y),
    std::max(a.z, b.z),
  };
}

inline Vec3 floor_vec3(const Vec3 v) {
  return Vec3{
    std::floor(v.x),
    std::floor(v.y),
    std::floor(v.z),
  };
}

inline std::array<f32, 4> to_vec4(const Vec3 v, const f32 w = 0.0f) {
  return std::array<f32, 4>{ v.x, v.y, v.z, w };
}

inline void runtime_assert(const bool condition, const string_view message) {
  if (!condition) {
    cerr << "Runtime assertion failed: " << message << endl;
    exit(EXIT_FAILURE);
  }
}

inline void vk_assert(const VkResult result, const string_view message) {
  if (result != VK_SUCCESS) {
    cerr << "Vk Assert failed: " << message << " (" << result << ")" << endl;
    exit(EXIT_FAILURE);
  }
}

inline u32 hash32(u32 x) {
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

inline u32 gf2_degree(u32 poly) {
  runtime_assert(poly != 0u, "Polynomial degree is undefined for zero");
  return 31u - static_cast<u32>(std::countl_zero(poly));
}

inline int gf2_degree64(u64 poly) {
  runtime_assert(poly != 0u, "Polynomial degree is undefined for zero");
  return 63 - static_cast<int>(std::countl_zero(poly));
}

inline u64 gf2_mod(u64 poly, u32 mod) {
  if (poly == 0u) {
    return 0u;
  }
  runtime_assert(mod != 0u, "Modulo polynomial must be non-zero");
  const int mod_degree = gf2_degree(mod);
  const u64 mod64 = static_cast<u64>(mod);
  while (poly != 0u) {
    const int poly_degree = gf2_degree64(poly);
    if (poly_degree < mod_degree) {
      break;
    }
    const int shift = poly_degree - mod_degree;
    poly ^= (mod64 << static_cast<u64>(shift));
  }
  return poly;
}

inline u32 gf2_mod_poly(u32 poly, u32 mod) {
  return static_cast<u32>(gf2_mod(poly, mod));
}

inline u32 gf2_mod_mul(u32 a, u32 b, u32 mod) {
  u64 product = 0u;
  u64 aa = static_cast<u64>(a);
  u32 bb = b;
  while (bb != 0u) {
    if ((bb & 1u) != 0u) {
      product ^= aa;
    }
    bb >>= 1u;
    aa <<= 1u;
  }
  return static_cast<u32>(gf2_mod(product, mod));
}

inline u32 gf2_mod_pow(u32 base, u64 exp, u32 mod) {
  u32 result = 1u;
  u32 power = base;
  while (exp != 0u) {
    if ((exp & 1u) != 0u) {
      result = gf2_mod_mul(result, power, mod);
    }
    exp >>= 1u;
    if (exp != 0u) {
      power = gf2_mod_mul(power, power, mod);
    }
  }
  return result;
}

inline u32 gf2_poly_gcd(u32 a, u32 b) {
  while (b != 0u) {
    const u32 r = gf2_mod_poly(a, b);
    a = b;
    b = r;
  }
  return a;
}

inline bool gf2_is_irreducible(u32 poly) {
  const u32 degree = gf2_degree(poly);
  if (degree <= 1u) {
    return true;
  }
  u32 x = 2u; // Polynomial "x"
  for (u32 i = 0u; i < degree / 2u; ++i) {
    x = gf2_mod_mul(x, x, poly);
    const u32 diff = x ^ 2u;
    if (gf2_poly_gcd(diff, poly) != 1u) {
      return false;
    }
  }
  return true;
}

inline std::vector<u64> unique_prime_factors(u64 n) {
  std::vector<u64> factors;
  for (u64 p = 2u; p * p <= n; ++p) {
    if ((n % p) != 0u) {
      continue;
    }
    factors.push_back(p);
    while ((n % p) == 0u) {
      n /= p;
    }
  }
  if (n > 1u) {
    factors.push_back(n);
  }
  return factors;
}

inline bool gf2_is_primitive(u32 poly) {
  if ((poly & 1u) == 0u) {
    return false;
  }
  const u32 degree = gf2_degree(poly);
  if (!gf2_is_irreducible(poly)) {
    return false;
  }
  const u64 order = (1ull << degree) - 1ull;
  const std::vector<u64> factors = unique_prime_factors(order);
  for (const u64 factor : factors) {
    const u64 exponent = order / factor;
    if (gf2_mod_pow(2u, exponent, poly) == 1u) {
      return false;
    }
  }
  return true;
}

inline std::vector<u32> generate_primitive_polynomials(u32 count) {
  std::vector<u32> polynomials;
  polynomials.reserve(count);
  for (u32 degree = 1u; polynomials.size() < count; ++degree) {
    const u32 start = (1u << degree) | 1u;
    const u32 end = 1u << (degree + 1u);
    for (u32 poly = start; poly < end; poly += 2u) {
      if (!gf2_is_primitive(poly)) {
        continue;
      }
      polynomials.push_back(poly);
      if (polynomials.size() == count) {
        break;
      }
    }
  }
  return polynomials;
}

inline std::vector<u32> generate_sobol_directions(u32 dimensions, u32 bits) {
  constexpr u32 MAX_BITS = 32u;
  runtime_assert(bits <= MAX_BITS, "Sobol generator supports up to 32 bits");
  std::vector<u32> result(dimensions * bits, 0u);
  const std::vector<u32> polynomials = generate_primitive_polynomials(dimensions);

  for (u32 dim = 0u; dim < dimensions; ++dim) {
    const u32 polynomial = polynomials[dim];
    const u32 degree = gf2_degree(polynomial);
    runtime_assert(degree <= bits, "Sobol polynomial degree exceeds direction bit count");

    std::array<u32, MAX_BITS> directions{};
    for (u32 i = 0u; i < degree; ++i) {
      const u32 limit = 1u << (i + 1u);
      u32 mk = hash32((dim + 1u) * 0x9E3779B9u ^ (i + 1u) * 0x94D049BBu);
      mk &= (limit - 1u);
      mk |= 1u;
      if (mk >= limit) {
        mk = limit - 1u;
      }
      if ((mk & 1u) == 0u) {
        mk ^= 1u;
      }
      directions[i] = mk << (MAX_BITS - (i + 1u));
    }

    const u32 mask = (degree > 1u) ? ((1u << (degree - 1u)) - 1u) : 0u;
    const u32 a = (degree > 1u) ? ((polynomial >> 1u) & mask) : 0u;
    for (u32 i = degree; i < bits; ++i) {
      u32 value = directions[i - degree] ^ (directions[i - degree] >> degree);
      for (u32 k = 1u; k < degree; ++k) {
        if ((a >> (degree - 1u - k)) & 1u) {
          value ^= directions[i - k];
        }
      }
      directions[i] = value;
    }

    for (u32 i = 0u; i < bits; ++i) {
      result[dim * bits + i] = directions[i];
    }
  }
  return result;
}

constexpr string_view APP_NAME = "callandor";
constexpr i32 WINDOW_WIDTH = 1280;
constexpr i32 WINDOW_HEIGHT = 720;

constexpr VkFormat SWAPCHAIN_FORMAT_PREFERRED = VK_FORMAT_B8G8R8A8_SRGB;
constexpr VkColorSpaceKHR SWAPCHAIN_COLOR_SPACE = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
constexpr VkFormat OFFSCREEN_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;

constexpr u32 LOCAL_SIZE_X = 8;
constexpr u32 LOCAL_SIZE_Y = 8;
constexpr u32 BUILDER_GROUP_SIZE = 256u;
constexpr u32 FRAMES_IN_FLIGHT = 2;

constexpr const char* SHADER_PATH_COMPUTE = "./shaders/compute.comp.spv";
constexpr const char* SHADER_PATH_L0_COUNT = "./shaders/grid_l0_count.comp.spv";
constexpr const char* SHADER_PATH_L0_OFFSETS = "./shaders/grid_l0_offsets.comp.spv";
constexpr const char* SHADER_PATH_L0_FILL = "./shaders/grid_l0_fill.comp.spv";
constexpr const char* SHADER_PATH_L1_COUNT = "./shaders/grid_l1_count.comp.spv";
constexpr const char* SHADER_PATH_L1_OFFSETS = "./shaders/grid_l1_offsets.comp.spv";
constexpr const char* SHADER_PATH_L1_FILL = "./shaders/grid_l1_fill.comp.spv";
constexpr const char* SHADER_PATH_VERT    = "./shaders/fullscreen.vert.spv";
constexpr const char* SHADER_PATH_FRAG    = "./shaders/blit.frag.spv";

constexpr usize MAX_SHADER_BYTES = 1u << 18; // 256 KiB per shader module

struct Caps {
  static constexpr u32 MaxPhys            = 8;
  static constexpr u32 MaxQF              = 16;
  static constexpr u32 MaxDevExt          = 256;
  static constexpr u32 MaxSurfaceFormats  = 64;
  static constexpr u32 MaxPresentModes    = 32;
  static constexpr u32 MaxSwapchainImages = 4;
  static constexpr bool StrictCaps        = true;
};

VkResult result = VK_SUCCESS;

#if defined(__APPLE__)
constexpr array<const char*, 3> instance_exts{
  VK_KHR_SURFACE_EXTENSION_NAME,
  VK_EXT_METAL_SURFACE_EXTENSION_NAME,
  VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
};
#elif defined(_WIN32)
constexpr array<const char*, 2> instance_exts{
  VK_KHR_SURFACE_EXTENSION_NAME,
  VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
};
#elif defined(__linux__)
constexpr array<const char*, 2> instance_exts{
  VK_KHR_SURFACE_EXTENSION_NAME,
  VK_KHR_XCB_SURFACE_EXTENSION_NAME,
};
#else
constexpr array<const char*, 1> instance_exts{
  VK_KHR_SURFACE_EXTENSION_NAME,
};
#endif

#if defined(__APPLE__)
constexpr array device_exts{
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME,
};
#else
constexpr array device_exts{
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};
#endif

constexpr array preferred_types{
  VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
  VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
  VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU,
  VK_PHYSICAL_DEVICE_TYPE_CPU,
};

constexpr VkPhysicalDeviceFeatures required_features{
  .samplerAnisotropy = VK_TRUE,
};

struct SwapchainBundle {
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{};
  u32 count = 0;
  array<VkImage, Caps::MaxSwapchainImages> images{};
  array<VkImageView, Caps::MaxSwapchainImages> views{};
  array<VkImageLayout, Caps::MaxSwapchainImages> layouts{};
};

struct OffscreenImage {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
};

struct FrameSync {
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkSemaphore image_available = VK_NULL_HANDLE;
  VkSemaphore render_finished = VK_NULL_HANDLE;
  VkFence in_flight = VK_NULL_HANDLE;
};

struct Buffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
};

struct SphereCPU {
  Vec3 center{};
  f32 radius = 1.0f;
  Vec3 albedo{};
};

struct SphereSoA {
  std::vector<std::array<f32, 4>> center_radius{};
  std::vector<std::array<f32, 4>> albedo{};
};

struct alignas(16) GridParamsGPU {
  std::array<f32, 4> bmin{};
  std::array<f32, 4> bmax{};
  std::array<u32, 4> dims{};
  std::array<f32, 4> inv_cell{};
};

struct GridStaticConfig {
  GridParamsGPU params{};
  u32 cell_count = 0u;
  u32 child_cell_count = 0u;
};

struct alignas(16) BuilderGlobals {
  u32 l0_total = 0u;
  u32 l1_total = 0u;
  u32 indices_capacity = 0u;
  u32 overflow = 0u;
};
static_assert(sizeof(BuilderGlobals) == sizeof(u32) * 4u, "BuilderGlobals layout mismatch");

struct CameraState {
  Vec3 position{};
  f32 yaw = 0.0f;
  f32 pitch = 0.0f;
};

struct alignas(16) CameraUniform {
  std::array<f32, 4> origin{};
  std::array<f32, 4> lower_left{};
  std::array<f32, 4> horizontal{};
  std::array<f32, 4> vertical{};
};

struct FrameTimeHistory {
  static constexpr usize MaxSamples = 600;
  std::vector<double> samples{};

  void push(const double ms) {
    if (samples.size() == MaxSamples) {
      samples.erase(samples.begin());
    }
    samples.push_back(ms);
  }

  [[nodiscard]] bool has_enough() const {
    return samples.size() >= 4;
  }

  [[nodiscard]] double percentile(const double p) const {
    if (samples.empty()) return 0.0;
    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    const double idx = p * static_cast<double>(sorted.size() - 1);
    const usize lo = static_cast<usize>(std::floor(idx));
    const usize hi = static_cast<usize>(std::ceil(idx));
    const double t = idx - static_cast<double>(lo);
    const double a = sorted[lo];
    const double b = sorted[std::min(hi, sorted.size() - 1)];
    return std::lerp(a, b, t);
  }

  [[nodiscard]] double average() const {
    if (samples.empty()) return 0.0;
    double sum = 0.0;
    for (const double v : samples) {
      sum += v;
    }
    return sum / static_cast<double>(samples.size());
  }

  void log_to_stdout() const {
    if (!has_enough()) return;
    const double p0 = percentile(0.0);
    const double p50 = percentile(0.5);
    const double p95 = percentile(0.95);
    const double p99 = percentile(0.99);
    const double avg = average();
    const double fps = (avg > 0.0) ? 1000.0 / avg : 0.0;
    std::cout << std::fixed << std::setprecision(2)
              << "[FrameTimes] count=" << samples.size()
              << " avg=" << avg << "ms"
              << " fps=" << fps
              << " p0=" << p0 << "ms"
              << " p50=" << p50 << "ms"
              << " p95=" << p95 << "ms"
              << " p99=" << p99 << "ms"
              << std::defaultfloat << std::endl;
  }
};

constexpr u32 SPHERE_COUNT = 100'000u;
static_assert(SPHERE_COUNT > 0u, "SPHERE_COUNT must be positive");
constexpr f32 GROUND_Y = -1.0f;
constexpr f32 CAMERA_DEFAULT_SPEED = 5.0f;
constexpr f32 CAMERA_BOOST_SPEED = 12.0f;
constexpr f32 CAMERA_MOUSE_SENSITIVITY = 0.0025f;
constexpr f32 CAMERA_FOV_DEGREES = 60.0f;
constexpr u32 PATH_MAX_BOUNCES = 4u;
constexpr u32 SOBOL_DIMENSIONS = 4u + 4u * PATH_MAX_BOUNCES;
constexpr u32 SOBOL_BITS = 32u;
constexpr u32 SOBOL_GLOBAL_SCRAMBLE = 0xA511E9B3u;
constexpr f32 SPHERE_RADIUS_MIN = 0.35f;
constexpr f32 SPHERE_RADIUS_MAX = 1.05f;
constexpr f32 SPHERE_HEIGHT_MIN = 0.2f;
constexpr f32 SPHERE_HEIGHT_MAX = 2.5f;
constexpr f32 SPHERE_TARGET_DENSITY = 0.45f;
constexpr usize GRID_MAX_LEAF = 16u;
constexpr u32 GRID_CHILD_DIM = 4u;

inline void init_glfw() {
  const int ok = glfwInit();
  runtime_assert(ok == GLFW_TRUE, "Failed to initialize GLFW");
  runtime_assert(glfwVulkanSupported() == GLFW_TRUE, "GLFW reports no Vulkan support");
}

inline GLFWwindow* create_window() {
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif
  GLFWwindow* win = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, APP_NAME.data(), nullptr, nullptr);
  runtime_assert(win != nullptr, "Failed to create GLFW window");
  return win;
}
inline VkInstance create_instance() {
  const VkApplicationInfo app_info{
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pApplicationName = APP_NAME.data(),
    .pEngineName = APP_NAME.data(),
    .apiVersion = VK_API_VERSION_1_3,
  };

  VkInstanceCreateInfo ici{
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo = &app_info,
    .enabledExtensionCount = static_cast<u32>(instance_exts.size()),
    .ppEnabledExtensionNames = instance_exts.data(),
  };

#if defined(__APPLE__)
  ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

  VkInstance inst = VK_NULL_HANDLE;
  result = vkCreateInstance(&ici, nullptr, &inst);
  return inst;
}

inline VkSurfaceKHR create_surface(VkInstance inst, GLFWwindow* win) {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  result = glfwCreateWindowSurface(inst, win, nullptr, &surface);
  return surface;
}

inline bool has_required_extensions(VkPhysicalDevice d) {
  u32 n = 0;
  VkResult r = vkEnumerateDeviceExtensionProperties(d, nullptr, &n, nullptr);
  if (r != VK_SUCCESS) return false;
  if (Caps::StrictCaps && n > Caps::MaxDevExt) return false;
  if (n > Caps::MaxDevExt) n = Caps::MaxDevExt;

  array<VkExtensionProperties, Caps::MaxDevExt> props{};
  r = vkEnumerateDeviceExtensionProperties(d, nullptr, &n, props.data());
  if (r != VK_SUCCESS && r != VK_INCOMPLETE) return false;

  for (const char* need : device_exts) {
    bool ok = false;
    for (u32 i = 0; i < n; ++i) {
      if (std::strcmp(props[i].extensionName, need) == 0) { ok = true; break; }
    }
    if (!ok) return false;
  }
  return true;
}

inline bool meets_required_features(VkPhysicalDevice d) {
  VkPhysicalDeviceFeatures f{};
  vkGetPhysicalDeviceFeatures(d, &f);
  if (required_features.samplerAnisotropy && !f.samplerAnisotropy) return false;
  return true;
}

inline bool queue_supports_present(VkPhysicalDevice d, u32 qf, VkSurfaceKHR surface) {
  VkBool32 present = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(d, qf, surface, &present);
  return present == VK_TRUE;
}

inline i32 first_graphics_compute_present_qf(VkPhysicalDevice d, VkSurfaceKHR surface) {
  u32 n = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(d, &n, nullptr);
  if (n == 0) return -1;
  if (Caps::StrictCaps && n > Caps::MaxQF) return -1;
  if (n > Caps::MaxQF) n = Caps::MaxQF;

  array<VkQueueFamilyProperties, Caps::MaxQF> qfs{};
  vkGetPhysicalDeviceQueueFamilyProperties(d, &n, qfs.data());

  for (u32 i = 0; i < n; ++i) {
    const VkQueueFamilyProperties& info = qfs[i];
    const bool has_gfx = (info.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    const bool has_cmp = (info.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
    if (!has_gfx || !has_cmp) continue;
    if (!queue_supports_present(d, i, surface)) continue;
    return static_cast<i32>(i);
  }
  return -1;
}

inline VkPhysicalDevice pick_device(VkInstance inst, VkSurfaceKHR surface, i32& out_qf) {
  u32 n = 0;
  VkResult r = vkEnumeratePhysicalDevices(inst, &n, nullptr);
  if (r != VK_SUCCESS || n == 0) return VK_NULL_HANDLE;
  if (Caps::StrictCaps && n > Caps::MaxPhys) return VK_NULL_HANDLE;
  if (n > Caps::MaxPhys) n = Caps::MaxPhys;

  array<VkPhysicalDevice, Caps::MaxPhys> devs{};
  r = vkEnumeratePhysicalDevices(inst, &n, devs.data());
  if (r != VK_SUCCESS) return VK_NULL_HANDLE;

  for (VkPhysicalDeviceType want : preferred_types) {
    for (u32 i = 0; i < n; ++i) {
      VkPhysicalDevice d = devs[i];
      VkPhysicalDeviceProperties p{};
      vkGetPhysicalDeviceProperties(d, &p);
      if (p.deviceType != want) continue;

      if (!has_required_extensions(d)) continue;
      if (!meets_required_features(d)) continue;

      const i32 gfx = first_graphics_compute_present_qf(d, surface);
      if (gfx < 0) continue;

      out_qf = gfx;
      return d;
    }
  }
  return VK_NULL_HANDLE;
}

inline VkDevice create_device(VkPhysicalDevice pd, u32 gfx_qf) {
  static constexpr f32 prio = 1.0f;
  const VkDeviceQueueCreateInfo qci{
    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueFamilyIndex = gfx_qf,
    .queueCount = 1,
    .pQueuePriorities = &prio,
  };
  const VkDeviceCreateInfo dci{
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount = 1u,
    .pQueueCreateInfos = &qci,
    .enabledExtensionCount = static_cast<u32>(device_exts.size()),
    .ppEnabledExtensionNames = device_exts.data(),
    .pEnabledFeatures = &required_features,
  };
  VkDevice device = VK_NULL_HANDLE;
  result = vkCreateDevice(pd, &dci, nullptr, &device);
  return device;
}

inline VkImageView create_image_view(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect) {
  const VkImageViewCreateInfo ici{
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .image = image,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = format,
    .components{
      .r = VK_COMPONENT_SWIZZLE_IDENTITY,
      .g = VK_COMPONENT_SWIZZLE_IDENTITY,
      .b = VK_COMPONENT_SWIZZLE_IDENTITY,
      .a = VK_COMPONENT_SWIZZLE_IDENTITY,
    },
    .subresourceRange{
      .aspectMask = aspect,
      .baseMipLevel = 0u,
      .levelCount = 1u,
      .baseArrayLayer = 0u,
      .layerCount = 1u,
    },
  };

  VkImageView view = VK_NULL_HANDLE;
  result = vkCreateImageView(device, &ici, nullptr, &view);
  vk_assert(result, "Failed to create image view");
  return view;
}

inline u32 find_memory_type_index(const VkPhysicalDeviceMemoryProperties& props, u32 type_bits, VkMemoryPropertyFlags required) {
  for (u32 i = 0; i < props.memoryTypeCount; ++i) {
    if ((type_bits & (1u << i)) == 0u) continue;
    if ((props.memoryTypes[i].propertyFlags & required) == required) return i;
  }
  return UINT32_MAX;
}

inline SwapchainBundle create_swapchain(VkPhysicalDevice pd, VkDevice device, VkSurfaceKHR surface, u32 gfx_qf, GLFWwindow* window) {
  SwapchainBundle bundle{};

  VkSurfaceCapabilitiesKHR caps{};
  vk_assert(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surface, &caps), "Failed to query surface capabilities");

  u32 format_count = 0;
  vk_assert(vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &format_count, nullptr), "Failed to query surface format count");
  runtime_assert(format_count > 0, "Surface offers no formats");
  if (Caps::StrictCaps) runtime_assert(format_count <= Caps::MaxSurfaceFormats, "Surface format count exceeds static cap");
  u32 limited_format_count = format_count;
  if (limited_format_count > Caps::MaxSurfaceFormats) limited_format_count = Caps::MaxSurfaceFormats;
  array<VkSurfaceFormatKHR, Caps::MaxSurfaceFormats> formats{};
  VkResult fmt_res = vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &limited_format_count, formats.data());
  runtime_assert(fmt_res == VK_SUCCESS || fmt_res == VK_INCOMPLETE, "Failed to query surface formats");
  runtime_assert(limited_format_count > 0, "No surface formats returned");

  VkSurfaceFormatKHR chosen_format = formats[0];
  for (u32 i = 0; i < limited_format_count; ++i) {
    if (formats[i].format == SWAPCHAIN_FORMAT_PREFERRED && formats[i].colorSpace == SWAPCHAIN_COLOR_SPACE) {
      chosen_format = formats[i];
      break;
    }
  }

  u32 present_mode_count = 0;
  vk_assert(vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &present_mode_count, nullptr), "Failed to query present mode count");
  runtime_assert(present_mode_count > 0, "Surface offers no present modes");
  if (Caps::StrictCaps) runtime_assert(present_mode_count <= Caps::MaxPresentModes, "Present mode count exceeds static cap");
  u32 limited_present_mode_count = present_mode_count;
  if (limited_present_mode_count > Caps::MaxPresentModes) limited_present_mode_count = Caps::MaxPresentModes;
  array<VkPresentModeKHR, Caps::MaxPresentModes> present_modes{};
  VkResult pm_res = vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &limited_present_mode_count, present_modes.data());
  runtime_assert(pm_res == VK_SUCCESS || pm_res == VK_INCOMPLETE, "Failed to query present modes");
  runtime_assert(limited_present_mode_count > 0, "No present modes returned");

  VkPresentModeKHR chosen_present = VK_PRESENT_MODE_FIFO_KHR;
  for (u32 i = 0; i < limited_present_mode_count; ++i) {
    if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
      chosen_present = VK_PRESENT_MODE_MAILBOX_KHR;
      break;
    }
  }

  VkExtent2D extent = {};
  if (caps.currentExtent.width != UINT32_MAX) {
    extent = caps.currentExtent;
  } else {
    i32 fb_width = 0;
    i32 fb_height = 0;
    glfwGetFramebufferSize(window, &fb_width, &fb_height);
    extent.width = static_cast<u32>(fb_width);
    extent.height = static_cast<u32>(fb_height);
    extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
  }

  u32 image_count = caps.minImageCount + 1u;
  if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) image_count = caps.maxImageCount;
  if (image_count > Caps::MaxSwapchainImages) image_count = Caps::MaxSwapchainImages;

  const VkSwapchainCreateInfoKHR sci{
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = surface,
    .minImageCount = image_count,
    .imageFormat = chosen_format.format,
    .imageColorSpace = chosen_format.colorSpace,
    .imageExtent = extent,
    .imageArrayLayers = 1u,
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0u,
    .pQueueFamilyIndices = nullptr,
    .preTransform = caps.currentTransform,
    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode = chosen_present,
    .clipped = VK_TRUE,
    .oldSwapchain = VK_NULL_HANDLE,
  };

  vk_assert(vkCreateSwapchainKHR(device, &sci, nullptr, &bundle.swapchain), "Failed to create swapchain");

  u32 retrieved = 0;
  vk_assert(vkGetSwapchainImagesKHR(device, bundle.swapchain, &retrieved, nullptr), "Failed to query swapchain image count");
  runtime_assert(retrieved <= Caps::MaxSwapchainImages, "Swapchain returned more images than supported");
  vk_assert(vkGetSwapchainImagesKHR(device, bundle.swapchain, &retrieved, bundle.images.data()), "Failed to get swapchain images");

  bundle.count = retrieved;
  bundle.format = chosen_format.format;
  bundle.extent = extent;
  bundle.layouts.fill(VK_IMAGE_LAYOUT_UNDEFINED);

  for (u32 i = 0; i < bundle.count; ++i) {
    bundle.views[i] = create_image_view(device, bundle.images[i], bundle.format, VK_IMAGE_ASPECT_COLOR_BIT);
  }

  return bundle;
}

inline OffscreenImage create_offscreen_image(VkPhysicalDevice pd, VkDevice device, VkExtent2D extent) {
  OffscreenImage off{};

  const VkImageCreateInfo ici{
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = OFFSCREEN_FORMAT,
    .extent = { extent.width, extent.height, 1u },
    .mipLevels = 1u,
    .arrayLayers = 1u,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  vk_assert(vkCreateImage(device, &ici, nullptr, &off.image), "Failed to create offscreen image");

  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(device, off.image, &req);

  VkPhysicalDeviceMemoryProperties mem_props{};
  vkGetPhysicalDeviceMemoryProperties(pd, &mem_props);

  const u32 memory_index = find_memory_type_index(mem_props, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  runtime_assert(memory_index != UINT32_MAX, "No suitable memory type for offscreen image");

  const VkMemoryAllocateInfo mai{
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = req.size,
    .memoryTypeIndex = memory_index,
  };
  vk_assert(vkAllocateMemory(device, &mai, nullptr, &off.memory), "Failed to allocate image memory");
  vk_assert(vkBindImageMemory(device, off.image, off.memory, 0u), "Failed to bind image memory");

  off.view = create_image_view(device, off.image, OFFSCREEN_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);

  const VkSamplerCreateInfo sci{
    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    .magFilter = VK_FILTER_LINEAR,
    .minFilter = VK_FILTER_LINEAR,
    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .mipLodBias = 0.0f,
    .maxAnisotropy = 1.0f,
    .minLod = 0.0f,
    .maxLod = 0.0f,
    .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
    .unnormalizedCoordinates = VK_FALSE,
  };
  vk_assert(vkCreateSampler(device, &sci, nullptr, &off.sampler), "Failed to create sampler");

  return off;
}

inline Buffer create_buffer(
  VkPhysicalDevice physical,
  VkDevice device,
  VkDeviceSize size,
  VkBufferUsageFlags usage,
  VkMemoryPropertyFlags properties
) {
  const VkBufferCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = size,
    .usage = usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };
  Buffer buffer{};
  buffer.size = size;
  vk_assert(vkCreateBuffer(device, &info, nullptr, &buffer.buffer), "Failed to create buffer");

  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(device, buffer.buffer, &req);

  VkPhysicalDeviceMemoryProperties mem_props{};
  vkGetPhysicalDeviceMemoryProperties(physical, &mem_props);
  const u32 memory_index = find_memory_type_index(mem_props, req.memoryTypeBits, properties);
  runtime_assert(memory_index != UINT32_MAX, "No suitable memory type for buffer");

  const VkMemoryAllocateInfo alloc_info{
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = req.size,
    .memoryTypeIndex = memory_index,
  };
  vk_assert(vkAllocateMemory(device, &alloc_info, nullptr, &buffer.memory), "Failed to allocate buffer memory");
  vk_assert(vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0u), "Failed to bind buffer memory");

  return buffer;
}

inline void destroy_buffer(VkDevice device, Buffer& buffer) {
  if (buffer.buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device, buffer.buffer, nullptr);
    buffer.buffer = VK_NULL_HANDLE;
  }
  if (buffer.memory != VK_NULL_HANDLE) {
    vkFreeMemory(device, buffer.memory, nullptr);
    buffer.memory = VK_NULL_HANDLE;
  }
  buffer.size = 0;
}

inline void* map_buffer(VkDevice device, Buffer& buffer, VkDeviceSize size) {
  void* ptr = nullptr;
  vk_assert(vkMapMemory(device, buffer.memory, 0u, size, 0u, &ptr), "Failed to map buffer memory");
  return ptr;
}

inline void unmap_buffer(VkDevice device, Buffer& buffer) {
  vkUnmapMemory(device, buffer.memory);
}

inline void copy_buffer(
  VkDevice device,
  VkCommandPool command_pool,
  VkQueue queue,
  VkBuffer src,
  VkBuffer dst,
  VkDeviceSize size
) {
  VkCommandBufferAllocateInfo alloc_info{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = command_pool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1u,
  };
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  vk_assert(vkAllocateCommandBuffers(device, &alloc_info, &cmd), "Failed to allocate copy command buffer");

  VkCommandBufferBeginInfo begin_info{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  vk_assert(vkBeginCommandBuffer(cmd, &begin_info), "Failed to begin copy command buffer");

  const VkBufferCopy region{
    .srcOffset = 0u,
    .dstOffset = 0u,
    .size = size,
  };
  vkCmdCopyBuffer(cmd, src, dst, 1u, &region);
  vk_assert(vkEndCommandBuffer(cmd), "Failed to end copy command buffer");

  const VkSubmitInfo submit_info{
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .commandBufferCount = 1u,
    .pCommandBuffers = &cmd,
  };
  vk_assert(vkQueueSubmit(queue, 1u, &submit_info, VK_NULL_HANDLE), "Failed to submit copy command buffer");
  vk_assert(vkQueueWaitIdle(queue), "Failed to wait for buffer copy");

  vkFreeCommandBuffers(device, command_pool, 1u, &cmd);
}

inline Buffer create_device_buffer_with_data(
  VkPhysicalDevice physical,
  VkDevice device,
  VkCommandPool upload_pool,
  VkQueue queue,
  VkDeviceSize size,
  VkBufferUsageFlags usage,
  const void* data
) {
  Buffer staging = create_buffer(
    physical,
    device,
    size,
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
  );
  {
    void* mapped = map_buffer(device, staging, staging.size);
    std::memcpy(mapped, data, static_cast<size_t>(staging.size));
    unmap_buffer(device, staging);
  }

  Buffer gpu = create_buffer(
    physical,
    device,
    size,
    usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
  );

  copy_buffer(device, upload_pool, queue, staging.buffer, gpu.buffer, size);
  destroy_buffer(device, staging);
  return gpu;
}

inline std::array<SphereCPU, SPHERE_COUNT> generate_random_spheres() {
  std::mt19937 rng(1337u);
  const f32 avg_radius = 0.5f * (SPHERE_RADIUS_MIN + SPHERE_RADIUS_MAX);
  const f32 target_area = (PI * avg_radius * avg_radius / SPHERE_TARGET_DENSITY) * static_cast<f32>(SPHERE_COUNT);
  const f32 base_extent = 6.0f;
  const f32 spawn_extent = std::max(base_extent, 0.5f * std::sqrt(target_area));
  std::uniform_real_distribution<f32> dist_xz(-spawn_extent, spawn_extent);
  std::uniform_real_distribution<f32> dist_radius(SPHERE_RADIUS_MIN, SPHERE_RADIUS_MAX);
  std::uniform_real_distribution<f32> dist_height(SPHERE_HEIGHT_MIN, SPHERE_HEIGHT_MAX);
  std::uniform_real_distribution<f32> dist_color(0.2f, 0.95f);

  std::array<SphereCPU, SPHERE_COUNT> spheres{};
  usize placed = 0;
  const f32 min_separation = avg_radius * 0.25f;
  u32 attempts = 0;

  while (placed < SPHERE_COUNT) {
    if (attempts++ > SPHERE_COUNT * 8192u) {
      runtime_assert(false, "Failed to place non-overlapping spheres");
    }

    const f32 radius = dist_radius(rng);
    const Vec3 candidate{
      dist_xz(rng),
      GROUND_Y + radius + dist_height(rng),
      dist_xz(rng),
    };

    bool overlaps = false;
    for (usize i = 0; i < placed; ++i) {
      const SphereCPU& other = spheres[i];
      const f32 required = radius + other.radius + min_separation;
      if (length_squared(candidate - other.center) < required * required) {
        overlaps = true;
        break;
      }
    }
    if (overlaps) continue;

    SphereCPU sphere{};
    sphere.center = candidate;
    sphere.radius = radius;
    sphere.albedo = Vec3{ dist_color(rng), dist_color(rng), dist_color(rng) };
    spheres[placed++] = sphere;
  }

  return spheres;
}

inline SphereSoA pack_spheres_gpu(const std::array<SphereCPU, SPHERE_COUNT>& cpu_spheres) {
  SphereSoA gpu{};
  gpu.center_radius.reserve(SPHERE_COUNT);
  gpu.albedo.reserve(SPHERE_COUNT);
  for (const SphereCPU& sphere : cpu_spheres) {
    gpu.center_radius.push_back(to_vec4(sphere.center, sphere.radius));
    gpu.albedo.push_back(to_vec4(sphere.albedo, 0.0f));
  }
  return gpu;
}

inline GridStaticConfig build_grid_static_config(const std::array<SphereCPU, SPHERE_COUNT>& spheres, const u32 sphere_count) {
  GridStaticConfig grid{};
  if (sphere_count == 0u) {
    grid.params.bmin = { 0.0f, 0.0f, 0.0f, 0.0f };
    grid.params.bmax = { 0.0f, 0.0f, 0.0f, 0.0f };
    grid.params.dims = { 1u, 1u, 1u, 0u };
    grid.params.inv_cell = { 0.0f, 0.0f, 0.0f, 0.0f };
    grid.cell_count = 1u;
    grid.child_cell_count = GRID_CHILD_DIM * GRID_CHILD_DIM * GRID_CHILD_DIM;
    return grid;
  }

  const f32 inf = std::numeric_limits<f32>::infinity();
  Vec3 scene_min{ inf, inf, inf };
  Vec3 scene_max{ -inf, -inf, -inf };

  for (u32 i = 0u; i < sphere_count; ++i) {
    const SphereCPU& sphere = spheres[i];
    const Vec3 radius_vec{ sphere.radius, sphere.radius, sphere.radius };
    scene_min = min_vec3(scene_min, sphere.center - radius_vec);
    scene_max = max_vec3(scene_max, sphere.center + radius_vec);
  }

  const f32 margin = 0.5f;
  const Vec3 margin_vec{ margin, margin, margin };
  scene_min -= margin_vec;
  scene_max += margin_vec;

  Vec3 ext = scene_max - scene_min;
  ext = max_vec3(ext, Vec3{ 0.1f, 0.1f, 0.1f });

  const f32 n_spheres = static_cast<f32>(std::max<u32>(sphere_count, 1u));
  const f32 total_cells_target = std::max(n_spheres / 4.0f, 1.0f);
  const f32 base = std::cbrt(total_cells_target);
  const f32 max_extent = std::max(ext.x, std::max(ext.y, ext.z));

  const Vec3 dims_f{
    std::max((ext.x / max_extent) * base, 1.0f),
    std::max((ext.y / max_extent) * base, 1.0f),
    std::max((ext.z / max_extent) * base, 1.0f),
  };

  const auto clamp_dim = [](const f32 value) -> u32 {
    return static_cast<u32>(std::clamp(std::round(value), 1.0f, 64.0f));
  };

  const u32 dims_x = clamp_dim(dims_f.x);
  const u32 dims_y = clamp_dim(dims_f.y);
  const u32 dims_z = clamp_dim(dims_f.z);

  const Vec3 cell_size{
    ext.x / static_cast<f32>(dims_x),
    ext.y / static_cast<f32>(dims_y),
    ext.z / static_cast<f32>(dims_z),
  };

  const Vec3 inv_cell{
    (std::abs(cell_size.x) > 1e-8f) ? 1.0f / cell_size.x : 0.0f,
    (std::abs(cell_size.y) > 1e-8f) ? 1.0f / cell_size.y : 0.0f,
    (std::abs(cell_size.z) > 1e-8f) ? 1.0f / cell_size.z : 0.0f,
  };

  const u32 cell_count = dims_x * dims_y * dims_z;
  runtime_assert(cell_count > 0u, "Grid has zero cells");
  const u32 child_cells_per_macro = GRID_CHILD_DIM * GRID_CHILD_DIM * GRID_CHILD_DIM;
  const u32 child_total = cell_count * child_cells_per_macro;

  grid.params.bmin = to_vec4(scene_min, 0.0f);
  grid.params.bmax = to_vec4(scene_max, 0.0f);
  grid.params.dims = { dims_x, dims_y, dims_z, 0u };
  grid.params.inv_cell = { inv_cell.x, inv_cell.y, inv_cell.z, 0.0f };
  grid.cell_count = cell_count;
  grid.child_cell_count = child_total;

  return grid;
}

inline Vec3 yaw_pitch_to_forward(const f32 yaw, const f32 pitch) {
  const f32 cos_pitch = std::cos(pitch);
  return Vec3{
    cos_pitch * std::cos(yaw),
    std::sin(pitch),
    cos_pitch * std::sin(yaw),
  };
}

inline CameraUniform build_camera_uniform(const CameraState& state, const f32 aspect_ratio, Vec3& out_forward, Vec3& out_right, Vec3& out_up) {
  out_forward = normalize(yaw_pitch_to_forward(state.yaw, state.pitch));
  const Vec3 world_up{ 0.0f, 1.0f, 0.0f };
  out_right = normalize(cross(out_forward, world_up));
  if (length_squared(out_right) < 1e-6f) {
    out_right = Vec3{ 1.0f, 0.0f, 0.0f };
  }
  out_up = normalize(cross(out_right, out_forward));

  const f32 theta = CAMERA_FOV_DEGREES * PI / 180.0f;
  const f32 h = std::tan(theta * 0.5f);
  const f32 viewport_height = 2.0f * h;
  const f32 viewport_width = aspect_ratio * viewport_height;

  const Vec3 horizontal = out_right * viewport_width;
  const Vec3 vertical = out_up * viewport_height;
  const Vec3 forward = out_forward;
  const Vec3 lower_left = state.position + forward - horizontal * 0.5f - vertical * 0.5f;

  CameraUniform uniform{};
  uniform.origin = to_vec4(state.position, 0.0f);
  uniform.lower_left = to_vec4(lower_left, 0.0f);
  uniform.horizontal = to_vec4(horizontal, 0.0f);
  uniform.vertical = to_vec4(vertical, 0.0f);
  return uniform;
}

inline VkDescriptorSetLayout create_descriptor_set_layout(VkDevice device) {
  const VkDescriptorSetLayoutBinding storage_binding{
    .binding = 0u,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  const VkDescriptorSetLayoutBinding sampled_binding{
    .binding = 1u,
    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
  };
  const VkDescriptorSetLayoutBinding centers_binding{
    .binding = 2u,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  const VkDescriptorSetLayoutBinding albedo_binding{
    .binding = 3u,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  const VkDescriptorSetLayoutBinding camera_binding{
    .binding = 4u,
    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  const VkDescriptorSetLayoutBinding grid_params_binding{
    .binding = 5u,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  const VkDescriptorSetLayoutBinding grid_l0_binding{
    .binding = 6u,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  const VkDescriptorSetLayoutBinding grid_l1_binding{
    .binding = 7u,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  const VkDescriptorSetLayoutBinding grid_indices_binding{
    .binding = 8u,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  const VkDescriptorSetLayoutBinding sobol_binding{
    .binding = 9u,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  const VkDescriptorSetLayoutBinding l0_counts_binding{
    .binding = 10u,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  const VkDescriptorSetLayoutBinding l0_offsets_binding{
    .binding = 11u,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  const VkDescriptorSetLayoutBinding l0_cursors_binding{
    .binding = 12u,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  const VkDescriptorSetLayoutBinding l1_counts_binding{
    .binding = 13u,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  const VkDescriptorSetLayoutBinding l1_offsets_binding{
    .binding = 14u,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  const VkDescriptorSetLayoutBinding l1_cursors_binding{
    .binding = 15u,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  const VkDescriptorSetLayoutBinding globals_binding{
    .binding = 17u,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  const array layout_bindings{
    storage_binding,
    sampled_binding,
    centers_binding,
    albedo_binding,
    camera_binding,
    grid_params_binding,
    grid_l0_binding,
    grid_l1_binding,
    grid_indices_binding,
    sobol_binding,
    l0_counts_binding,
    l0_offsets_binding,
    l0_cursors_binding,
    l1_counts_binding,
    l1_offsets_binding,
    l1_cursors_binding,
    globals_binding
  };
  const VkDescriptorSetLayoutCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = static_cast<u32>(layout_bindings.size()),
    .pBindings = layout_bindings.data(),
  };
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  vk_assert(vkCreateDescriptorSetLayout(device, &info, nullptr, &layout), "Failed to create descriptor set layout");
  return layout;
}

inline VkDescriptorPool create_descriptor_pool(VkDevice device) {
  const VkDescriptorPoolSize pool_sizes[]{
    {
      .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .descriptorCount = 1u,
    },
    {
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1u,
    },
    {
      .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 16u,
    },
    {
      .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .descriptorCount = 1u,
    },
  };
  const VkDescriptorPoolCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = 1u,
    .poolSizeCount = static_cast<u32>(std::size(pool_sizes)),
    .pPoolSizes = pool_sizes,
  };
  VkDescriptorPool pool = VK_NULL_HANDLE;
  vk_assert(vkCreateDescriptorPool(device, &info, nullptr, &pool), "Failed to create descriptor pool");
  return pool;
}

inline VkDescriptorSet allocate_descriptor_set(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout) {
  const VkDescriptorSetAllocateInfo info{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool = pool,
    .descriptorSetCount = 1u,
    .pSetLayouts = &layout,
  };
  VkDescriptorSet set = VK_NULL_HANDLE;
  vk_assert(vkAllocateDescriptorSets(device, &info, &set), "Failed to allocate descriptor set");
  return set;
}

inline void write_descriptor_set(
  VkDevice device,
  VkDescriptorSet set,
  const OffscreenImage& offscreen,
  const Buffer& centers_buffer,
  const Buffer& albedo_buffer,
  const Buffer& camera_buffer,
  const Buffer& grid_params_buffer,
  const Buffer& grid_l0_buffer,
  const Buffer& grid_l1_buffer,
  const Buffer& grid_indices_buffer,
  const Buffer& sobol_buffer,
  const Buffer& l0_counts_buffer,
  const Buffer& l0_offsets_buffer,
  const Buffer& l0_cursors_buffer,
  const Buffer& l1_counts_buffer,
  const Buffer& l1_offsets_buffer,
  const Buffer& l1_cursors_buffer,
  const Buffer& globals_buffer
) {
  const VkDescriptorImageInfo storage_info{
    .sampler = VK_NULL_HANDLE,
    .imageView = offscreen.view,
    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
  };
  const VkDescriptorImageInfo sampler_info{
    .sampler = offscreen.sampler,
    .imageView = offscreen.view,
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };
  const VkDescriptorBufferInfo center_info{
    .buffer = centers_buffer.buffer,
    .offset = 0u,
    .range = centers_buffer.size,
  };
  const VkDescriptorBufferInfo albedo_info{
    .buffer = albedo_buffer.buffer,
    .offset = 0u,
    .range = albedo_buffer.size,
  };
  const VkDescriptorBufferInfo camera_info{
    .buffer = camera_buffer.buffer,
    .offset = 0u,
    .range = camera_buffer.size,
  };
  const VkDescriptorBufferInfo grid_params_info{
    .buffer = grid_params_buffer.buffer,
    .offset = 0u,
    .range = grid_params_buffer.size,
  };
  const VkDescriptorBufferInfo grid_l0_info{
    .buffer = grid_l0_buffer.buffer,
    .offset = 0u,
    .range = grid_l0_buffer.size,
  };
  const VkDescriptorBufferInfo grid_l1_info{
    .buffer = grid_l1_buffer.buffer,
    .offset = 0u,
    .range = grid_l1_buffer.size,
  };
  const VkDescriptorBufferInfo grid_indices_info{
    .buffer = grid_indices_buffer.buffer,
    .offset = 0u,
    .range = grid_indices_buffer.size,
  };
  const VkDescriptorBufferInfo sobol_info{
    .buffer = sobol_buffer.buffer,
    .offset = 0u,
    .range = sobol_buffer.size,
  };
  const VkDescriptorBufferInfo l0_counts_info{
    .buffer = l0_counts_buffer.buffer,
    .offset = 0u,
    .range = l0_counts_buffer.size,
  };
  const VkDescriptorBufferInfo l0_offsets_info{
    .buffer = l0_offsets_buffer.buffer,
    .offset = 0u,
    .range = l0_offsets_buffer.size,
  };
  const VkDescriptorBufferInfo l0_cursors_info{
    .buffer = l0_cursors_buffer.buffer,
    .offset = 0u,
    .range = l0_cursors_buffer.size,
  };
  const VkDescriptorBufferInfo l1_counts_info{
    .buffer = l1_counts_buffer.buffer,
    .offset = 0u,
    .range = l1_counts_buffer.size,
  };
  const VkDescriptorBufferInfo l1_offsets_info{
    .buffer = l1_offsets_buffer.buffer,
    .offset = 0u,
    .range = l1_offsets_buffer.size,
  };
  const VkDescriptorBufferInfo l1_cursors_info{
    .buffer = l1_cursors_buffer.buffer,
    .offset = 0u,
    .range = l1_cursors_buffer.size,
  };
  const VkDescriptorBufferInfo globals_info{
    .buffer = globals_buffer.buffer,
    .offset = 0u,
    .range = globals_buffer.size,
  };
  const VkWriteDescriptorSet writes[]{
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 0u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .pImageInfo = &storage_info,
    },
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 1u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImageInfo = &sampler_info,
    },
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 2u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &center_info,
    },
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 3u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &albedo_info,
    },
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 4u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .pBufferInfo = &camera_info,
    },
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 5u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &grid_params_info,
    },
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 6u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &grid_l0_info,
    },
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 7u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &grid_l1_info,
    },
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 8u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &grid_indices_info,
    },
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 9u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &sobol_info,
    },
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 10u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &l0_counts_info,
    },
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 11u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &l0_offsets_info,
    },
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 12u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &l0_cursors_info,
    },
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 13u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &l1_counts_info,
    },
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 14u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &l1_offsets_info,
    },
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 15u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &l1_cursors_info,
    },
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 17u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &globals_info,
    },
  };
  vkUpdateDescriptorSets(device, static_cast<u32>(std::size(writes)), writes, 0u, nullptr);
}

inline VkShaderModule load_shader_module(VkDevice device, const char* path) {
  std::array<u32, MAX_SHADER_BYTES / sizeof(u32)> buffer{};
  std::FILE* file = std::fopen(path, "rb");
  runtime_assert(file != nullptr, "Failed to open shader file");

  const size_t bytes_read = std::fread(buffer.data(), 1, MAX_SHADER_BYTES, file);
  runtime_assert(bytes_read > 0, "Shader file is empty");
  runtime_assert(bytes_read < MAX_SHADER_BYTES, "Shader file exceeds static buffer");
  runtime_assert((bytes_read % sizeof(u32)) == 0u, "Shader byte count is not aligned");
  const bool read_error = std::ferror(file) != 0;
  std::fclose(file);
  runtime_assert(!read_error, "Error reading shader file");

  const VkShaderModuleCreateInfo smci{
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = bytes_read,
    .pCode = buffer.data(),
  };

  VkShaderModule module = VK_NULL_HANDLE;
  vk_assert(vkCreateShaderModule(device, &smci, nullptr, &module), "Failed to create shader module");
  return module;
}

struct alignas(16) ComputePushConstants {
  std::array<f32, 4> screen{};
  std::array<u32, 4> frame{};
};
static_assert(sizeof(ComputePushConstants) <= 128, "Push constants exceed spec limit");

inline VkPipelineLayout create_compute_pipeline_layout(VkDevice device, VkDescriptorSetLayout layout) {
  const VkPushConstantRange push_range{
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    .offset = 0u,
    .size = static_cast<u32>(sizeof(ComputePushConstants)),
  };
  const VkPipelineLayoutCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1u,
    .pSetLayouts = &layout,
    .pushConstantRangeCount = 1u,
    .pPushConstantRanges = &push_range,
  };
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  vk_assert(vkCreatePipelineLayout(device, &info, nullptr, &pipeline_layout), "Failed to create compute pipeline layout");
  return pipeline_layout;
}

inline VkPipelineLayout create_graphics_pipeline_layout(VkDevice device, VkDescriptorSetLayout layout) {
  const VkPipelineLayoutCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1u,
    .pSetLayouts = &layout,
  };
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  vk_assert(vkCreatePipelineLayout(device, &info, nullptr, &pipeline_layout), "Failed to create graphics pipeline layout");
  return pipeline_layout;
}

inline VkPipeline create_compute_pipeline(VkDevice device, VkPipelineLayout layout, VkShaderModule shader) {
  const VkPipelineShaderStageCreateInfo stage{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
    .module = shader,
    .pName = "main",
  };
  const VkComputePipelineCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage = stage,
    .layout = layout,
  };
  VkPipeline pipeline = VK_NULL_HANDLE;
  vk_assert(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1u, &info, nullptr, &pipeline), "Failed to create compute pipeline");
  return pipeline;
}

inline VkRenderPass create_render_pass(VkDevice device, VkFormat format) {
  const VkAttachmentDescription color_attachment{
    .format = format,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  };
  const VkAttachmentReference color_ref{
    .attachment = 0u,
    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };
  const VkSubpassDescription subpass{
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount = 1u,
    .pColorAttachments = &color_ref,
  };
  const VkSubpassDependency dependency{
    .srcSubpass = VK_SUBPASS_EXTERNAL,
    .dstSubpass = 0u,
    .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .srcAccessMask = 0u,
    .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
  };
  const VkRenderPassCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .attachmentCount = 1u,
    .pAttachments = &color_attachment,
    .subpassCount = 1u,
    .pSubpasses = &subpass,
    .dependencyCount = 1u,
    .pDependencies = &dependency,
  };
  VkRenderPass render_pass = VK_NULL_HANDLE;
  vk_assert(vkCreateRenderPass(device, &info, nullptr, &render_pass), "Failed to create render pass");
  return render_pass;
}

inline VkPipeline create_graphics_pipeline(
  VkDevice device,
  VkPipelineLayout layout,
  VkRenderPass render_pass,
  VkShaderModule vert_module,
  VkShaderModule frag_module
) {
  const VkPipelineShaderStageCreateInfo stages[]{
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vert_module,
      .pName = "main",
    },
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = frag_module,
      .pName = "main",
    },
  };
  const VkPipelineVertexInputStateCreateInfo vertex_input{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };
  const VkPipelineInputAssemblyStateCreateInfo input_assembly{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .primitiveRestartEnable = VK_FALSE,
  };
  const VkPipelineViewportStateCreateInfo viewport_state{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .viewportCount = 1u,
    .pViewports = nullptr,
    .scissorCount = 1u,
    .pScissors = nullptr,
  };
  const VkPipelineRasterizationStateCreateInfo raster_state{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_NONE,
    .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    .lineWidth = 1.0f,
  };
  const VkPipelineMultisampleStateCreateInfo multisample_state{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
  };
  const VkPipelineColorBlendAttachmentState blend_attachment{
    .blendEnable = VK_FALSE,
    .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
    .colorBlendOp = VK_BLEND_OP_ADD,
    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
    .alphaBlendOp = VK_BLEND_OP_ADD,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
  };
  const VkPipelineColorBlendStateCreateInfo blend_state{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .attachmentCount = 1u,
    .pAttachments = &blend_attachment,
  };
  constexpr array dynamic_states{
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
  };
  const VkPipelineDynamicStateCreateInfo dynamic_state{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
    .pDynamicStates = dynamic_states.data(),
  };
  const VkGraphicsPipelineCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount = static_cast<u32>(std::size(stages)),
    .pStages = stages,
    .pVertexInputState = &vertex_input,
    .pInputAssemblyState = &input_assembly,
    .pViewportState = &viewport_state,
    .pRasterizationState = &raster_state,
    .pMultisampleState = &multisample_state,
    .pColorBlendState = &blend_state,
    .pDynamicState = &dynamic_state,
    .layout = layout,
    .renderPass = render_pass,
    .subpass = 0u,
  };
  VkPipeline pipeline = VK_NULL_HANDLE;
  vk_assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &info, nullptr, &pipeline), "Failed to create graphics pipeline");
  return pipeline;
}

inline void create_framebuffers(
  VkDevice device,
  VkRenderPass render_pass,
  const SwapchainBundle& swapchain,
  array<VkFramebuffer, Caps::MaxSwapchainImages>& framebuffers
) {
  for (u32 i = 0; i < swapchain.count; ++i) {
    const VkImageView attachments[]{ swapchain.views[i] };
    const VkFramebufferCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass = render_pass,
      .attachmentCount = 1u,
      .pAttachments = attachments,
      .width = swapchain.extent.width,
      .height = swapchain.extent.height,
      .layers = 1u,
    };
    vk_assert(vkCreateFramebuffer(device, &info, nullptr, &framebuffers[i]), "Failed to create framebuffer");
  }
}

inline VkCommandPool create_command_pool(VkDevice device, u32 queue_family, VkCommandPoolCreateFlags flags) {
  const VkCommandPoolCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = flags,
    .queueFamilyIndex = queue_family,
  };
  VkCommandPool pool = VK_NULL_HANDLE;
  vk_assert(vkCreateCommandPool(device, &info, nullptr, &pool), "Failed to create command pool");
  return pool;
}

inline array<VkCommandBuffer, FRAMES_IN_FLIGHT> allocate_command_buffers(VkDevice device, VkCommandPool pool) {
  array<VkCommandBuffer, FRAMES_IN_FLIGHT> buffers{};
  const VkCommandBufferAllocateInfo info{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = pool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = FRAMES_IN_FLIGHT,
  };
  vk_assert(vkAllocateCommandBuffers(device, &info, buffers.data()), "Failed to allocate command buffers");
  return buffers;
}

inline void initialize_frame_sync_objects(
  VkDevice device,
  const array<VkCommandBuffer, FRAMES_IN_FLIGHT>& buffers,
  array<FrameSync, FRAMES_IN_FLIGHT>& frames
) {
  const VkSemaphoreCreateInfo semaphore_info{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  };
  const VkFenceCreateInfo fence_info{
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    .flags = VK_FENCE_CREATE_SIGNALED_BIT,
  };
  for (u32 i = 0; i < FRAMES_IN_FLIGHT; ++i) {
    frames[i].cmd = buffers[i];
    vk_assert(vkCreateSemaphore(device, &semaphore_info, nullptr, &frames[i].image_available), "Failed to create semaphore");
    vk_assert(vkCreateSemaphore(device, &semaphore_info, nullptr, &frames[i].render_finished), "Failed to create semaphore");
    vk_assert(vkCreateFence(device, &fence_info, nullptr, &frames[i].in_flight), "Failed to create fence");
  }
}

int main() {
  init_glfw();
  GLFWwindow* window = create_window();
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  if (glfwRawMouseMotionSupported()) {
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
  }
  double last_mouse_x = 0.0;
  double last_mouse_y = 0.0;
  bool first_mouse = true;

  VkInstance instance = create_instance();
  vk_assert(result, "Failed to create instance");

  VkSurfaceKHR surface = create_surface(instance, window);
  vk_assert(result, "Failed to create surface");

  i32 gfx_qf = -1;
  VkPhysicalDevice physical = pick_device(instance, surface, gfx_qf);
  runtime_assert(physical != VK_NULL_HANDLE, "No suitable physical device");
  runtime_assert(gfx_qf >= 0, "No graphics/compute/present queue family found");

  VkDevice device = create_device(physical, static_cast<u32>(gfx_qf));
  vk_assert(result, "Failed to create device");

  VkQueue queue = VK_NULL_HANDLE;
  vkGetDeviceQueue(device, static_cast<u32>(gfx_qf), 0u, &queue);
  runtime_assert(queue != VK_NULL_HANDLE, "No queue returned");

  SwapchainBundle swapchain = create_swapchain(physical, device, surface, static_cast<u32>(gfx_qf), window);
  runtime_assert(swapchain.count > 0u, "Swapchain has no images");

  OffscreenImage offscreen = create_offscreen_image(physical, device, swapchain.extent);

  VkCommandPool upload_pool = create_command_pool(device, static_cast<u32>(gfx_qf), VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

  const auto spheres_cpu = generate_random_spheres();
  const SphereSoA spheres_gpu = pack_spheres_gpu(spheres_cpu);
  const u32 sphere_count = static_cast<u32>(spheres_gpu.center_radius.size());
  Buffer sphere_centers = create_device_buffer_with_data(
    physical,
    device,
    upload_pool,
    queue,
    static_cast<VkDeviceSize>(spheres_gpu.center_radius.size() * sizeof(spheres_gpu.center_radius.front())),
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    spheres_gpu.center_radius.data()
  );
  Buffer sphere_albedo = create_device_buffer_with_data(
    physical,
    device,
    upload_pool,
    queue,
    static_cast<VkDeviceSize>(spheres_gpu.albedo.size() * sizeof(spheres_gpu.albedo.front())),
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    spheres_gpu.albedo.data()
  );

  const GridStaticConfig grid_config = build_grid_static_config(spheres_cpu, sphere_count);
  runtime_assert(grid_config.cell_count > 0u, "Grid L0 cell count must be positive");
  runtime_assert(grid_config.child_cell_count > 0u, "Grid L1 cell count must be positive");
  const u32 child_cells_per_macro = GRID_CHILD_DIM * GRID_CHILD_DIM * GRID_CHILD_DIM;
  runtime_assert(
    grid_config.child_cell_count == grid_config.cell_count * child_cells_per_macro,
    "Unexpected grid child cell count"
  );

  Buffer grid_params_buffer = create_device_buffer_with_data(
    physical,
    device,
    upload_pool,
    queue,
    static_cast<VkDeviceSize>(sizeof(GridParamsGPU)),
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    &grid_config.params
  );

  const u32 indices_capacity = SPHERE_COUNT * 16u;
  const u32 sphere_group_count = (sphere_count + BUILDER_GROUP_SIZE - 1u) / BUILDER_GROUP_SIZE;
  const u32 l0_group_count = (grid_config.cell_count + BUILDER_GROUP_SIZE - 1u) / BUILDER_GROUP_SIZE;
  const u32 l1_group_count = (grid_config.child_cell_count + BUILDER_GROUP_SIZE - 1u) / BUILDER_GROUP_SIZE;
  const VkDeviceSize grid_l0_size = static_cast<VkDeviceSize>(grid_config.cell_count) * sizeof(u32) * 4u;
  const VkDeviceSize grid_l1_size = static_cast<VkDeviceSize>(grid_config.child_cell_count) * sizeof(u32) * 2u;
  const VkDeviceSize l0_scalar_size = static_cast<VkDeviceSize>(grid_config.cell_count) * sizeof(u32);
  const VkDeviceSize l1_scalar_size = static_cast<VkDeviceSize>(grid_config.child_cell_count) * sizeof(u32);
  const VkDeviceSize indices_size = static_cast<VkDeviceSize>(indices_capacity) * sizeof(u32);

  Buffer grid_l0_buffer = create_buffer(
    physical,
    device,
    grid_l0_size,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
  );
  Buffer grid_l1_buffer = create_buffer(
    physical,
    device,
    grid_l1_size,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
  );
  Buffer grid_indices_buffer = create_buffer(
    physical,
    device,
    indices_size,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
  );
  Buffer l0_counts_buffer = create_buffer(
    physical,
    device,
    l0_scalar_size,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
  );
  Buffer l0_offsets_buffer = create_buffer(
    physical,
    device,
    l0_scalar_size,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
  );
  Buffer l0_cursors_buffer = create_buffer(
    physical,
    device,
    l0_scalar_size,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
  );
  Buffer l1_counts_buffer = create_buffer(
    physical,
    device,
    l1_scalar_size,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
  );
  Buffer l1_offsets_buffer = create_buffer(
    physical,
    device,
    l1_scalar_size,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
  );
  Buffer l1_cursors_buffer = create_buffer(
    physical,
    device,
    l1_scalar_size,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
  );
  Buffer globals_buffer = create_buffer(
    physical,
    device,
    static_cast<VkDeviceSize>(sizeof(BuilderGlobals)),
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
  );
  auto* builder_globals_ptr = static_cast<BuilderGlobals*>(map_buffer(device, globals_buffer, globals_buffer.size));
  builder_globals_ptr->l0_total = 0u;
  builder_globals_ptr->l1_total = 0u;
  builder_globals_ptr->indices_capacity = indices_capacity;
  builder_globals_ptr->overflow = 0u;
  const std::vector<u32> sobol_dirs_cpu = generate_sobol_directions(SOBOL_DIMENSIONS, SOBOL_BITS);
  Buffer sobol_dirs_buffer = create_device_buffer_with_data(
    physical,
    device,
    upload_pool,
    queue,
    static_cast<VkDeviceSize>(sobol_dirs_cpu.size() * sizeof(u32)),
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    sobol_dirs_cpu.data()
  );
  vkDestroyCommandPool(device, upload_pool, nullptr);

  Buffer camera_buffer = create_buffer(
    physical,
    device,
    sizeof(CameraUniform),
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
  );
  auto* camera_uniform_ptr = static_cast<CameraUniform*>(map_buffer(device, camera_buffer, camera_buffer.size));

  VkDescriptorSetLayout descriptor_layout = create_descriptor_set_layout(device);
  VkDescriptorPool descriptor_pool = create_descriptor_pool(device);
  VkDescriptorSet descriptor_set = allocate_descriptor_set(device, descriptor_pool, descriptor_layout);
  write_descriptor_set(
    device,
    descriptor_set,
    offscreen,
    sphere_centers,
    sphere_albedo,
    camera_buffer,
    grid_params_buffer,
    grid_l0_buffer,
    grid_l1_buffer,
    grid_indices_buffer,
    sobol_dirs_buffer,
    l0_counts_buffer,
    l0_offsets_buffer,
    l0_cursors_buffer,
    l1_counts_buffer,
    l1_offsets_buffer,
    l1_cursors_buffer,
    globals_buffer
  );

  CameraState camera_state{};
  camera_state.position = Vec3{ 0.0f, 1.5f, 6.0f };
  camera_state.yaw = 0.0f;
  camera_state.pitch = 0.0f;

  Vec3 camera_forward{};
  Vec3 camera_right{};
  Vec3 camera_up{};
  const f32 aspect_ratio = static_cast<f32>(swapchain.extent.width) / static_cast<f32>(swapchain.extent.height);
  const CameraUniform initial_camera_uniform = build_camera_uniform(camera_state, aspect_ratio, camera_forward, camera_right, camera_up);
  *camera_uniform_ptr = initial_camera_uniform;

  VkShaderModule l0_count_module = load_shader_module(device, SHADER_PATH_L0_COUNT);
  VkShaderModule l0_offsets_module = load_shader_module(device, SHADER_PATH_L0_OFFSETS);
  VkShaderModule l0_fill_module = load_shader_module(device, SHADER_PATH_L0_FILL);
  VkShaderModule l1_count_module = load_shader_module(device, SHADER_PATH_L1_COUNT);
  VkShaderModule l1_offsets_module = load_shader_module(device, SHADER_PATH_L1_OFFSETS);
  VkShaderModule l1_fill_module = load_shader_module(device, SHADER_PATH_L1_FILL);
  VkShaderModule compute_module = load_shader_module(device, SHADER_PATH_COMPUTE);
  VkShaderModule vert_module = load_shader_module(device, SHADER_PATH_VERT);
  VkShaderModule frag_module = load_shader_module(device, SHADER_PATH_FRAG);

  VkPipelineLayout compute_layout = create_compute_pipeline_layout(device, descriptor_layout);
  VkPipelineLayout graphics_layout = create_graphics_pipeline_layout(device, descriptor_layout);
  VkPipeline l0_count_pipeline = create_compute_pipeline(device, compute_layout, l0_count_module);
  VkPipeline l0_offsets_pipeline = create_compute_pipeline(device, compute_layout, l0_offsets_module);
  VkPipeline l0_fill_pipeline = create_compute_pipeline(device, compute_layout, l0_fill_module);
  VkPipeline l1_count_pipeline = create_compute_pipeline(device, compute_layout, l1_count_module);
  VkPipeline l1_offsets_pipeline = create_compute_pipeline(device, compute_layout, l1_offsets_module);
  VkPipeline l1_fill_pipeline = create_compute_pipeline(device, compute_layout, l1_fill_module);
  VkPipeline compute_pipeline = create_compute_pipeline(device, compute_layout, compute_module);
  VkRenderPass render_pass = create_render_pass(device, swapchain.format);
  VkPipeline graphics_pipeline = create_graphics_pipeline(device, graphics_layout, render_pass, vert_module, frag_module);

  array<VkFramebuffer, Caps::MaxSwapchainImages> framebuffers{};
  create_framebuffers(device, render_pass, swapchain, framebuffers);

  // Shader modules no longer needed once pipelines are created.
  vkDestroyShaderModule(device, frag_module, nullptr);
  vkDestroyShaderModule(device, vert_module, nullptr);
  vkDestroyShaderModule(device, compute_module, nullptr);
  vkDestroyShaderModule(device, l1_fill_module, nullptr);
  vkDestroyShaderModule(device, l1_offsets_module, nullptr);
  vkDestroyShaderModule(device, l1_count_module, nullptr);
  vkDestroyShaderModule(device, l0_fill_module, nullptr);
  vkDestroyShaderModule(device, l0_offsets_module, nullptr);
  vkDestroyShaderModule(device, l0_count_module, nullptr);

  VkCommandPool command_pool = create_command_pool(device, static_cast<u32>(gfx_qf), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
  array<VkCommandBuffer, FRAMES_IN_FLIGHT> command_buffers = allocate_command_buffers(device, command_pool);
  array<FrameSync, FRAMES_IN_FLIGHT> frames{};
  initialize_frame_sync_objects(device, command_buffers, frames);

  FrameTimeHistory frame_times{};
  u32 frames_since_log = 0u;
  const auto start_time = chrono::steady_clock::now();
  auto last_frame_time = start_time;
  bool running = true;
  bool first_compute = true;
  u32 frame_cursor = 0u;
  u64 frame_counter = 0u;

  while (running) {
    glfwPollEvents();
    running = glfwWindowShouldClose(window) == GLFW_FALSE;
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      running = false;
    }
    if (!running) break;

    const auto frame_begin = chrono::steady_clock::now();
    const chrono::duration<f32> delta = frame_begin - last_frame_time;
    last_frame_time = frame_begin;
    const f32 dt = std::clamp(delta.count(), 0.0f, 0.1f);

    double current_mouse_x = 0.0;
    double current_mouse_y = 0.0;
    glfwGetCursorPos(window, &current_mouse_x, &current_mouse_y);
    if (first_mouse) {
      last_mouse_x = current_mouse_x;
      last_mouse_y = current_mouse_y;
      first_mouse = false;
    }
    const double delta_x = current_mouse_x - last_mouse_x;
    const double delta_y = current_mouse_y - last_mouse_y;
    last_mouse_x = current_mouse_x;
    last_mouse_y = current_mouse_y;

    bool camera_updated = false;
    if (delta_x != 0.0 || delta_y != 0.0) {
      camera_state.yaw += static_cast<f32>(delta_x) * CAMERA_MOUSE_SENSITIVITY;
      camera_state.pitch += static_cast<f32>(-delta_y) * CAMERA_MOUSE_SENSITIVITY;
      camera_state.pitch = std::clamp(camera_state.pitch, -PI * 0.49f, PI * 0.49f);
      camera_updated = true;
    }

    Vec3 move_forward = normalize(yaw_pitch_to_forward(camera_state.yaw, camera_state.pitch));
    Vec3 move_right = normalize(cross(move_forward, Vec3{ 0.0f, 1.0f, 0.0f }));
    if (length_squared(move_right) < 1e-6f) {
      move_right = Vec3{ 1.0f, 0.0f, 0.0f };
    }
    Vec3 move_up = normalize(cross(move_right, move_forward));

    const f32 move_speed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
      ? CAMERA_BOOST_SPEED
      : CAMERA_DEFAULT_SPEED;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
      camera_state.position += move_forward * move_speed * dt;
      camera_updated = true;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
      camera_state.position -= move_forward * move_speed * dt;
      camera_updated = true;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
      camera_state.position -= move_right * move_speed * dt;
      camera_updated = true;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
      camera_state.position += move_right * move_speed * dt;
      camera_updated = true;
    }
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
      camera_state.position -= move_up * move_speed * dt;
      camera_updated = true;
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
      camera_state.position += move_up * move_speed * dt;
      camera_updated = true;
    }

    if (camera_updated) {
      const CameraUniform updated_uniform = build_camera_uniform(camera_state, aspect_ratio, camera_forward, camera_right, camera_up);
      *camera_uniform_ptr = updated_uniform;
      frame_counter = 0u;
    }

    FrameSync& frame = frames[frame_cursor];
    vk_assert(vkWaitForFences(device, 1u, &frame.in_flight, VK_TRUE, UINT64_MAX), "Failed waiting for fence");
    vk_assert(vkResetFences(device, 1u, &frame.in_flight), "Failed to reset fence");
    runtime_assert(builder_globals_ptr->overflow == 0u, "Grid index buffer overflow; increase capacity");
    builder_globals_ptr->l0_total = 0u;
    builder_globals_ptr->l1_total = 0u;
    builder_globals_ptr->indices_capacity = indices_capacity;
    builder_globals_ptr->overflow = 0u;

    u32 image_index = 0;
    const VkResult acquire = vkAcquireNextImageKHR(device, swapchain.swapchain, UINT64_MAX, frame.image_available, VK_NULL_HANDLE, &image_index);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
      // Minimal implementation: exit main loop on resize/out-of-date.
      running = false;
      break;
    }
    vk_assert(acquire, "Failed to acquire swapchain image");

    vk_assert(vkResetCommandBuffer(frame.cmd, 0u), "Failed to reset command buffer");

    const VkCommandBufferBeginInfo cbbi{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vk_assert(vkBeginCommandBuffer(frame.cmd, &cbbi), "Failed to begin command buffer");

    const VkImageMemoryBarrier to_storage{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = first_compute ? 0u : VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      .oldLayout = first_compute ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL,
      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = offscreen.image,
      .subresourceRange{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0u,
        .levelCount = 1u,
        .baseArrayLayer = 0u,
        .layerCount = 1u,
      },
    };
    vkCmdPipelineBarrier(
      frame.cmd,
      first_compute ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0u,
      0u, nullptr,
      0u, nullptr,
      1u, &to_storage
    );
    first_compute = false;

    vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_layout, 0u, 1u, &descriptor_set, 0u, nullptr);

    ComputePushConstants push{};
    push.screen = {
      1.0f / static_cast<f32>(swapchain.extent.width),
      1.0f / static_cast<f32>(swapchain.extent.height),
      0.0f,
      0.0f
    };
    push.frame = {
      static_cast<u32>(frame_counter),
      SOBOL_GLOBAL_SCRAMBLE,
      sphere_count,
      1u
    };
    vkCmdPushConstants(frame.cmd, compute_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, static_cast<u32>(sizeof(ComputePushConstants)), &push);

    vkCmdFillBuffer(frame.cmd, l0_counts_buffer.buffer, 0u, l0_scalar_size, 0u);
    vkCmdFillBuffer(frame.cmd, l0_offsets_buffer.buffer, 0u, l0_scalar_size, 0u);
    vkCmdFillBuffer(frame.cmd, l0_cursors_buffer.buffer, 0u, l0_scalar_size, 0u);
    vkCmdFillBuffer(frame.cmd, l1_counts_buffer.buffer, 0u, l1_scalar_size, 0u);
    vkCmdFillBuffer(frame.cmd, l1_offsets_buffer.buffer, 0u, l1_scalar_size, 0u);
    vkCmdFillBuffer(frame.cmd, l1_cursors_buffer.buffer, 0u, l1_scalar_size, 0u);

    const VkMemoryBarrier fill_barrier{
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
    };
    vkCmdPipelineBarrier(
      frame.cmd,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0u,
      1u,
      &fill_barrier,
      0u,
      nullptr,
      0u,
      nullptr
    );

    const VkMemoryBarrier compute_barrier{
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
    };

    vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, l0_count_pipeline);
    vkCmdDispatch(frame.cmd, sphere_group_count, 1u, 1u);
    vkCmdPipelineBarrier(
      frame.cmd,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0u,
      1u,
      &compute_barrier,
      0u,
      nullptr,
      0u,
      nullptr
    );

    vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, l0_offsets_pipeline);
    vkCmdDispatch(frame.cmd, l0_group_count, 1u, 1u);
    vkCmdPipelineBarrier(
      frame.cmd,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0u,
      1u,
      &compute_barrier,
      0u,
      nullptr,
      0u,
      nullptr
    );

    vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, l1_count_pipeline);
    vkCmdDispatch(frame.cmd, sphere_group_count, 1u, 1u);
    vkCmdPipelineBarrier(
      frame.cmd,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0u,
      1u,
      &compute_barrier,
      0u,
      nullptr,
      0u,
      nullptr
    );

    vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, l1_offsets_pipeline);
    vkCmdDispatch(frame.cmd, l1_group_count, 1u, 1u);
    vkCmdPipelineBarrier(
      frame.cmd,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0u,
      1u,
      &compute_barrier,
      0u,
      nullptr,
      0u,
      nullptr
    );

    vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, l0_fill_pipeline);
    vkCmdDispatch(frame.cmd, sphere_group_count, 1u, 1u);
    vkCmdPipelineBarrier(
      frame.cmd,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0u,
      1u,
      &compute_barrier,
      0u,
      nullptr,
      0u,
      nullptr
    );

    vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, l1_fill_pipeline);
    vkCmdDispatch(frame.cmd, sphere_group_count, 1u, 1u);
    vkCmdPipelineBarrier(
      frame.cmd,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0u,
      1u,
      &compute_barrier,
      0u,
      nullptr,
      0u,
      nullptr
    );

    vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);
    vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_layout, 0u, 1u, &descriptor_set, 0u, nullptr);
    vkCmdPushConstants(frame.cmd, compute_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, static_cast<u32>(sizeof(ComputePushConstants)), &push);

    const u32 group_count_x = (swapchain.extent.width + LOCAL_SIZE_X - 1u) / LOCAL_SIZE_X;
    const u32 group_count_y = (swapchain.extent.height + LOCAL_SIZE_Y - 1u) / LOCAL_SIZE_Y;
    vkCmdDispatch(frame.cmd, group_count_x, group_count_y, 1u);

    const VkImageMemoryBarrier to_sampled{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = offscreen.image,
      .subresourceRange{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0u,
        .levelCount = 1u,
        .baseArrayLayer = 0u,
        .layerCount = 1u,
      },
    };
    vkCmdPipelineBarrier(
      frame.cmd,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0u,
      0u, nullptr,
      0u, nullptr,
      1u, &to_sampled
    );

    const VkImageLayout previous_layout = swapchain.layouts[image_index];
    const VkImageMemoryBarrier swap_to_color{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = 0u,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .oldLayout = previous_layout,
      .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = swapchain.images[image_index],
      .subresourceRange{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0u,
        .levelCount = 1u,
        .baseArrayLayer = 0u,
        .layerCount = 1u,
      },
    };
    const VkPipelineStageFlags swap_src_stage =
      (previous_layout == VK_IMAGE_LAYOUT_UNDEFINED)
        ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
        : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    vkCmdPipelineBarrier(
      frame.cmd,
      swap_src_stage,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      0u,
      0u, nullptr,
      0u, nullptr,
      1u, &swap_to_color
    );

    const VkClearValue clear_color{ .color{ {0.0f, 0.0f, 0.0f, 1.0f} } };
    const VkRenderPassBeginInfo rp_begin{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = render_pass,
      .framebuffer = framebuffers[image_index],
      .renderArea{
        .offset{ 0, 0 },
        .extent = swapchain.extent,
      },
      .clearValueCount = 1u,
      .pClearValues = &clear_color,
    };
    vkCmdBeginRenderPass(frame.cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
    vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_layout, 0u, 1u, &descriptor_set, 0u, nullptr);

    const VkViewport viewport{
      .x = 0.0f,
      .y = 0.0f,
      .width = static_cast<f32>(swapchain.extent.width),
      .height = static_cast<f32>(swapchain.extent.height),
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
    };
    const VkRect2D scissor{
      .offset{ 0, 0 },
      .extent = swapchain.extent,
    };
    vkCmdSetViewport(frame.cmd, 0u, 1u, &viewport);
    vkCmdSetScissor(frame.cmd, 0u, 1u, &scissor);
    vkCmdDraw(frame.cmd, 3u, 1u, 0u, 0u);
    vkCmdEndRenderPass(frame.cmd);

    const VkImageMemoryBarrier back_to_general{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = offscreen.image,
      .subresourceRange{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0u,
        .levelCount = 1u,
        .baseArrayLayer = 0u,
        .layerCount = 1u,
      },
    };
    vkCmdPipelineBarrier(
      frame.cmd,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0u,
      0u, nullptr,
      0u, nullptr,
      1u, &back_to_general
    );

    vk_assert(vkEndCommandBuffer(frame.cmd), "Failed to end command buffer");

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submit{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount = 1u,
      .pWaitSemaphores = &frame.image_available,
      .pWaitDstStageMask = &wait_stage,
      .commandBufferCount = 1u,
      .pCommandBuffers = &frame.cmd,
      .signalSemaphoreCount = 1u,
      .pSignalSemaphores = &frame.render_finished,
    };
    vk_assert(vkQueueSubmit(queue, 1u, &submit, frame.in_flight), "Failed to submit queue");

    const VkPresentInfoKHR present_info{
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1u,
      .pWaitSemaphores = &frame.render_finished,
      .swapchainCount = 1u,
      .pSwapchains = &swapchain.swapchain,
      .pImageIndices = &image_index,
    };
    const VkResult present_res = vkQueuePresentKHR(queue, &present_info);
    if (present_res == VK_ERROR_OUT_OF_DATE_KHR || present_res == VK_SUBOPTIMAL_KHR) {
      running = false;
    } else {
      vk_assert(present_res, "Failed to present swapchain image");
    }

    swapchain.layouts[image_index] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    frame_cursor = (frame_cursor + 1u) % FRAMES_IN_FLIGHT;
    ++frame_counter;

    const auto frame_end = chrono::steady_clock::now();
    const double frame_ms = chrono::duration<double, std::milli>(frame_end - frame_begin).count();
    frame_times.push(frame_ms);
    ++frames_since_log;
    if (frames_since_log >= 120u) {
      frame_times.log_to_stdout();
      frames_since_log = 0u;
    }
  }

  vkDeviceWaitIdle(device);

  for (FrameSync& frame : frames) {
    vkDestroySemaphore(device, frame.render_finished, nullptr);
    vkDestroySemaphore(device, frame.image_available, nullptr);
    vkDestroyFence(device, frame.in_flight, nullptr);
  }

  vkFreeCommandBuffers(device, command_pool, FRAMES_IN_FLIGHT, command_buffers.data());
  vkDestroyCommandPool(device, command_pool, nullptr);

  vkDestroyPipeline(device, graphics_pipeline, nullptr);
  vkDestroyPipeline(device, l1_fill_pipeline, nullptr);
  vkDestroyPipeline(device, l1_offsets_pipeline, nullptr);
  vkDestroyPipeline(device, l1_count_pipeline, nullptr);
  vkDestroyPipeline(device, l0_fill_pipeline, nullptr);
  vkDestroyPipeline(device, l0_offsets_pipeline, nullptr);
  vkDestroyPipeline(device, l0_count_pipeline, nullptr);
  vkDestroyPipeline(device, compute_pipeline, nullptr);
  vkDestroyPipelineLayout(device, graphics_layout, nullptr);
  vkDestroyPipelineLayout(device, compute_layout, nullptr);

  for (u32 i = 0; i < swapchain.count; ++i) {
    vkDestroyFramebuffer(device, framebuffers[i], nullptr);
  }

  vkDestroyRenderPass(device, render_pass, nullptr);

  vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);

  unmap_buffer(device, camera_buffer);
  unmap_buffer(device, globals_buffer);
  destroy_buffer(device, camera_buffer);
  destroy_buffer(device, globals_buffer);
  destroy_buffer(device, l1_cursors_buffer);
  destroy_buffer(device, l1_offsets_buffer);
  destroy_buffer(device, l1_counts_buffer);
  destroy_buffer(device, l0_cursors_buffer);
  destroy_buffer(device, l0_offsets_buffer);
  destroy_buffer(device, l0_counts_buffer);
  destroy_buffer(device, sobol_dirs_buffer);
  destroy_buffer(device, grid_indices_buffer);
  destroy_buffer(device, grid_l1_buffer);
  destroy_buffer(device, grid_l0_buffer);
  destroy_buffer(device, grid_params_buffer);
  destroy_buffer(device, sphere_albedo);
  destroy_buffer(device, sphere_centers);

  vkDestroySampler(device, offscreen.sampler, nullptr);
  vkDestroyImageView(device, offscreen.view, nullptr);
  vkDestroyImage(device, offscreen.image, nullptr);
  vkFreeMemory(device, offscreen.memory, nullptr);

  for (u32 i = 0; i < swapchain.count; ++i) {
    vkDestroyImageView(device, swapchain.views[i], nullptr);
  }
  vkDestroySwapchainKHR(device, swapchain.swapchain, nullptr);

  vkDestroyDevice(device, nullptr);
  vkDestroySurfaceKHR(instance, surface, nullptr);
  vkDestroyInstance(instance, nullptr);

  glfwDestroyWindow(window);
  glfwTerminate();

  return EXIT_SUCCESS;
}
