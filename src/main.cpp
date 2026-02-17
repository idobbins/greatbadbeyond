#include "triangle_comp_spv.h"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdint>

constexpr uint32_t WINDOW_WIDTH = 1280;
constexpr uint32_t WINDOW_HEIGHT = 720;
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

constexpr uint32_t MAX_INSTANCE_EXTENSIONS = 16;
constexpr uint32_t MAX_DEVICE_EXTENSIONS = 4;
constexpr uint32_t MAX_PHYSICAL_DEVICES = 8;
constexpr uint32_t MAX_SWAPCHAIN_IMAGES = 3;
constexpr uint32_t ARENA_HEADER_WORDS = 32;
constexpr uint32_t BRICK_WORDS = 2;
constexpr float TEST_BRICK_VOXEL_SIZE = 0.5f;
constexpr float BRICK_WORLD_SIZE = TEST_BRICK_VOXEL_SIZE * 4.0f;
constexpr float SCENE_GRID_MIN_X = -64.0f;
constexpr float SCENE_GRID_MIN_Y = -12.0f;
constexpr float SCENE_GRID_MIN_Z = -64.0f;
constexpr uint32_t SCENE_GRID_DIM_X = 64;
constexpr uint32_t SCENE_GRID_DIM_Y = 12;
constexpr uint32_t SCENE_GRID_DIM_Z = 64;
constexpr uint32_t SCENE_GRID_CELL_COUNT = SCENE_GRID_DIM_X * SCENE_GRID_DIM_Y * SCENE_GRID_DIM_Z;
constexpr uint32_t MACRO_BRICK_DIM = 4;
constexpr uint32_t MACRO_GRID_DIM_X = (SCENE_GRID_DIM_X + (MACRO_BRICK_DIM - 1)) / MACRO_BRICK_DIM;
constexpr uint32_t MACRO_GRID_DIM_Y = (SCENE_GRID_DIM_Y + (MACRO_BRICK_DIM - 1)) / MACRO_BRICK_DIM;
constexpr uint32_t MACRO_GRID_DIM_Z = (SCENE_GRID_DIM_Z + (MACRO_BRICK_DIM - 1)) / MACRO_BRICK_DIM;
constexpr uint32_t MACRO_GRID_CELL_COUNT = MACRO_GRID_DIM_X * MACRO_GRID_DIM_Y * MACRO_GRID_DIM_Z;
constexpr uint32_t BRICK_TABLE_WORDS = SCENE_GRID_CELL_COUNT;
constexpr uint32_t MACRO_MASK_WORDS = MACRO_GRID_CELL_COUNT * BRICK_WORDS;
constexpr uint32_t BRICK_POOL_CAPACITY = SCENE_GRID_CELL_COUNT;
constexpr uint32_t ARENA_BRICK_TABLE_BASE_WORD = ARENA_HEADER_WORDS;
constexpr uint32_t ARENA_MACRO_MASK_BASE_WORD = ARENA_BRICK_TABLE_BASE_WORD + BRICK_TABLE_WORDS;
constexpr uint32_t ARENA_BRICK_POOL_BASE_WORD = ARENA_MACRO_MASK_BASE_WORD + MACRO_MASK_WORDS;
constexpr uint32_t SLOT_WORDS = ARENA_HEADER_WORDS + BRICK_TABLE_WORDS + MACRO_MASK_WORDS + BRICK_WORDS * BRICK_POOL_CAPACITY;
constexpr uint32_t EMPTY_BRICK_SLOT = 0xFFFFFFFFu;

constexpr uint32_t HDR_CAM_POS_X = 0;
constexpr uint32_t HDR_CAM_POS_Y = 1;
constexpr uint32_t HDR_CAM_POS_Z = 2;
constexpr uint32_t HDR_CAM_YAW = 3;
constexpr uint32_t HDR_CAM_PITCH = 4;
constexpr uint32_t HDR_CAM_MOVE_SPEED = 5;
constexpr uint32_t HDR_CAM_MOUSE_SENSITIVITY = 6;
constexpr uint32_t HDR_CAM_FRAME_INDEX = 7;
constexpr uint32_t HDR_CAM_FOV_Y = 8;
constexpr uint32_t HDR_GRID_MIN_X = 9;
constexpr uint32_t HDR_GRID_MIN_Y = 10;
constexpr uint32_t HDR_GRID_MIN_Z = 11;
constexpr uint32_t HDR_GRID_DIM_X = 12;
constexpr uint32_t HDR_GRID_DIM_Y = 13;
constexpr uint32_t HDR_GRID_DIM_Z = 14;
constexpr uint32_t HDR_BRICK_COUNT = 15;
constexpr uint32_t HDR_BRICK_TABLE_OFFSET_WORDS = 16;
constexpr uint32_t HDR_BRICK_POOL_OFFSET_WORDS = 17;
constexpr uint32_t HDR_CAM_FORWARD_X = 18;
constexpr uint32_t HDR_CAM_FORWARD_Y = 19;
constexpr uint32_t HDR_CAM_FORWARD_Z = 20;
constexpr uint32_t HDR_CAM_RIGHT_X = 21;
constexpr uint32_t HDR_CAM_RIGHT_Y = 22;
constexpr uint32_t HDR_CAM_RIGHT_Z = 23;
constexpr uint32_t HDR_CAM_UP_X = 24;
constexpr uint32_t HDR_CAM_UP_Y = 25;
constexpr uint32_t HDR_CAM_UP_Z = 26;
constexpr uint32_t HDR_CAM_TAN_HALF_FOV_Y = 27;
constexpr uint32_t HDR_BRICK_VOXEL_SIZE = 28;
constexpr uint32_t HDR_MACRO_MASK_OFFSET_WORDS = 29;

constexpr float CAMERA_MOVE_SPEED = 3.25f;
constexpr float CAMERA_MOUSE_SENSITIVITY = 0.0024f;
constexpr float CAMERA_FOV_Y = 1.0471976f;
constexpr float CAMERA_SPEED_BOOST_MULTIPLIER = 3.0f;
constexpr double CAMERA_FIXED_STEP_SECONDS = 1.0 / 120.0;
constexpr double CAMERA_MAX_FRAME_DELTA_SECONDS = 0.05;
constexpr uint32_t CAMERA_MAX_FIXED_STEPS = 8;
constexpr float TERRAIN_NOISE_SCALE = 0.045f;
constexpr float TERRAIN_BASE_HEIGHT = -2.0f;
constexpr float TERRAIN_HEIGHT_RANGE = 10.0f;
constexpr uint32_t TERRAIN_HASH_SEED_X = 0x1f123bb5u;
constexpr uint32_t TERRAIN_HASH_SEED_Z = 0x9e3779b9u;

constexpr uint32_t DATA_WORD_COUNT = SLOT_WORDS * MAX_FRAMES_IN_FLIGHT;
constexpr VkDeviceSize DATA_BUFFER_SIZE = static_cast<VkDeviceSize>(DATA_WORD_COUNT) * sizeof(uint32_t);

static_assert(MAX_FRAMES_IN_FLIGHT == 3);
static_assert(MAX_SWAPCHAIN_IMAGES >= MAX_FRAMES_IN_FLIGHT);
static_assert(SCENE_GRID_CELL_COUNT <= BRICK_TABLE_WORDS);
static_assert(HDR_BRICK_POOL_OFFSET_WORDS < ARENA_HEADER_WORDS);
static_assert(HDR_BRICK_VOXEL_SIZE < ARENA_HEADER_WORDS);
static_assert(HDR_MACRO_MASK_OFFSET_WORDS < ARENA_HEADER_WORDS);
static_assert((ARENA_HEADER_WORDS * sizeof(uint32_t)) <= 128);
static_assert((ARENA_MACRO_MASK_BASE_WORD + MACRO_MASK_WORDS) <= ARENA_BRICK_POOL_BASE_WORD);
static_assert((ARENA_BRICK_POOL_BASE_WORD + BRICK_WORDS * BRICK_POOL_CAPACITY) <= SLOT_WORDS);
static_assert((kTriangleCompSpv_size != 0));
static_assert((kTriangleCompSpv_size % 4) == 0);

constexpr VkInstanceCreateFlags PORTABILITY_ENUMERATE_FLAG = 0x00000001;
constexpr const char *PORTABILITY_ENUMERATION_EXTENSION = "VK_KHR_portability_enumeration";
constexpr const char *PORTABILITY_SUBSET_EXTENSION = "VK_KHR_portability_subset";

#if defined(__APPLE__)
constexpr uint32_t EXTRA_INSTANCE_EXTENSION_COUNT = 1;
constexpr const char *EXTRA_INSTANCE_EXTENSIONS[EXTRA_INSTANCE_EXTENSION_COUNT] = {
    PORTABILITY_ENUMERATION_EXTENSION,
};
constexpr uint32_t EXTRA_DEVICE_EXTENSION_COUNT = 1;
constexpr const char *EXTRA_DEVICE_EXTENSIONS[EXTRA_DEVICE_EXTENSION_COUNT] = {
    PORTABILITY_SUBSET_EXTENSION,
};
#else
constexpr uint32_t EXTRA_INSTANCE_EXTENSION_COUNT = 0;
constexpr const char **EXTRA_INSTANCE_EXTENSIONS = nullptr;
constexpr uint32_t EXTRA_DEVICE_EXTENSION_COUNT = 0;
constexpr const char **EXTRA_DEVICE_EXTENSIONS = nullptr;
#endif

static_assert(EXTRA_INSTANCE_EXTENSION_COUNT <= MAX_INSTANCE_EXTENSIONS);
static_assert((1 + EXTRA_DEVICE_EXTENSION_COUNT) <= MAX_DEVICE_EXTENSIONS);

GLFWwindow *window = nullptr;

VkInstance instance = VK_NULL_HANDLE;
VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
VkDevice device = VK_NULL_HANDLE;
VkQueue graphicsQueue = VK_NULL_HANDLE;

VkSurfaceKHR surface = VK_NULL_HANDLE;
VkSwapchainKHR swapchain = VK_NULL_HANDLE;
VkExtent2D swapExtent{};
uint32_t swapImageCount = 0;

std::array<VkImage, MAX_SWAPCHAIN_IMAGES> swapImages{};
std::array<VkImageView, MAX_SWAPCHAIN_IMAGES> swapImageViews{};
std::array<uint8_t, MAX_SWAPCHAIN_IMAGES> swapImagePresented{};

VkImage renderImage = VK_NULL_HANDLE;
VkDeviceMemory renderImageMemory = VK_NULL_HANDLE;
VkImageView renderImageView = VK_NULL_HANDLE;
bool renderImageInitialized = false;

VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> descriptorSets{};

VkBuffer dataBuffer = VK_NULL_HANDLE;
VkDeviceMemory dataBufferMemory = VK_NULL_HANDLE;
uint32_t *dataBufferWords = nullptr;

VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
VkPipeline computePipeline = VK_NULL_HANDLE;

VkCommandPool commandPool = VK_NULL_HANDLE;
std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> commandBuffers{};

std::array<VkFence, MAX_FRAMES_IN_FLIGHT> inFlightFences{};
std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> imageAvailableSemaphores{};
std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> renderFinishedSemaphores{};

float cameraPosX = 0.0f;
float cameraPosY = 9.0f;
float cameraPosZ = 0.0f;
float cameraYaw = -1.5707963f;
float cameraPitch = 0.0f;
double lastMouseX = 0.0;
double lastMouseY = 0.0;
bool mouseInitialized = false;
bool cameraTimeInitialized = false;
double lastCameraSampleTime = 0.0;
double cameraFixedAccumulatorSeconds = 0.0;
double accumulatedMouseDeltaX = 0.0;
double accumulatedMouseDeltaY = 0.0;
uint32_t frameCounter = 0;
uint32_t sceneBrickCount = 0;

uint32_t FindMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags requiredFlags)
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
    {
        const uint32_t typeMatch = (typeBits & (1u << i));
        const uint32_t flagsMatch = (memoryProperties.memoryTypes[i].propertyFlags & requiredFlags) == requiredFlags;
        if (typeMatch != 0 && flagsMatch != 0)
        {
            return i;
        }
    }

    return 0;
}

constexpr uint32_t GridLinearIndex(uint32_t x, uint32_t y, uint32_t z)
{
    return x + y * SCENE_GRID_DIM_X + z * SCENE_GRID_DIM_X * SCENE_GRID_DIM_Y;
}

constexpr uint32_t MacroLinearIndex(uint32_t x, uint32_t y, uint32_t z)
{
    return x + y * MACRO_GRID_DIM_X + z * MACRO_GRID_DIM_X * MACRO_GRID_DIM_Y;
}

// 2-bit Morton packing for x/y/z in [0,3], yielding bit index [0,63].
constexpr uint32_t BrickBitIndex(uint32_t x, uint32_t y, uint32_t z)
{
    const uint32_t lowBits = (x & 1u) | ((y & 1u) << 1u) | ((z & 1u) << 2u);
    const uint32_t highBits = ((x & 2u) << 2u) | ((y & 2u) << 3u) | ((z & 2u) << 4u);
    return lowBits | highBits;
}

uint32_t HashMix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint32_t Hash2D(uint32_t x, uint32_t z)
{
    return HashMix32((x * TERRAIN_HASH_SEED_X) ^ (z * TERRAIN_HASH_SEED_Z) ^ 0x85ebca6bu);
}

float HashToUnit(uint32_t h)
{
    constexpr float inv = 1.0f / 16777215.0f;
    return static_cast<float>(h & 0x00FFFFFFu) * inv;
}

float Smooth01(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

float ValueNoise2D(float x, float z)
{
    const float floorX = std::floor(x);
    const float floorZ = std::floor(z);
    const uint32_t ix0 = static_cast<uint32_t>(static_cast<int32_t>(floorX) + 32768);
    const uint32_t iz0 = static_cast<uint32_t>(static_cast<int32_t>(floorZ) + 32768);
    const uint32_t ix1 = ix0 + 1u;
    const uint32_t iz1 = iz0 + 1u;
    const float fx = x - floorX;
    const float fz = z - floorZ;
    const float u = Smooth01(fx);
    const float v = Smooth01(fz);
    const float n00 = HashToUnit(Hash2D(ix0, iz0));
    const float n10 = HashToUnit(Hash2D(ix1, iz0));
    const float n01 = HashToUnit(Hash2D(ix0, iz1));
    const float n11 = HashToUnit(Hash2D(ix1, iz1));
    return Lerp(Lerp(n00, n10, u), Lerp(n01, n11, u), v);
}

float TerrainHeight(float wx, float wz)
{
    const float nx = wx * TERRAIN_NOISE_SCALE + 23.0f;
    const float nz = wz * TERRAIN_NOISE_SCALE + 41.0f;
    float amp = 1.0f;
    float freq = 1.0f;
    float h = 0.0f;
    float norm = 0.0f;
    for (uint32_t octave = 0; octave < 4u; octave++)
    {
        h += ValueNoise2D(nx * freq, nz * freq) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    h /= norm;
    const float ridge = 1.0f - std::fabs(h * 2.0f - 1.0f);
    const float shaped = h * 0.72f + ridge * 0.28f;
    return TERRAIN_BASE_HEIGHT + shaped * TERRAIN_HEIGHT_RANGE;
}

uint64_t BuildTerrainBrickMask(float brickMinX, float brickMinY, float brickMinZ)
{
    uint64_t occupancy = 0;
    for (uint32_t z = 0; z < 4u; z++)
    {
        for (uint32_t y = 0; y < 4u; y++)
        {
            for (uint32_t x = 0; x < 4u; x++)
            {
                const float wx = brickMinX + (static_cast<float>(x) + 0.5f) * TEST_BRICK_VOXEL_SIZE;
                const float wy = brickMinY + (static_cast<float>(y) + 0.5f) * TEST_BRICK_VOXEL_SIZE;
                const float wz = brickMinZ + (static_cast<float>(z) + 0.5f) * TEST_BRICK_VOXEL_SIZE;
                const float height = TerrainHeight(wx, wz);
                const bool filled = wy <= height;
                const uint32_t bitIndex = BrickBitIndex(x, y, z);
                occupancy |= filled ? (1ull << bitIndex) : 0ull;
            }
        }
    }
    return occupancy;
}

void UpdateFlightCamera()
{
    const double now = glfwGetTime();
    if (!cameraTimeInitialized)
    {
        cameraTimeInitialized = true;
        lastCameraSampleTime = now;
    }
    double deltaTimeSeconds = now - lastCameraSampleTime;
    lastCameraSampleTime = now;
    deltaTimeSeconds = std::fmax(0.0, std::fmin(deltaTimeSeconds, CAMERA_MAX_FRAME_DELTA_SECONDS));

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    if (!mouseInitialized)
    {
        mouseInitialized = true;
        lastMouseX = mouseX;
        lastMouseY = mouseY;
    }

    const double mouseDeltaX = mouseX - lastMouseX;
    const double mouseDeltaY = mouseY - lastMouseY;
    lastMouseX = mouseX;
    lastMouseY = mouseY;

    accumulatedMouseDeltaX += mouseDeltaX;
    accumulatedMouseDeltaY += mouseDeltaY;

    cameraFixedAccumulatorSeconds += deltaTimeSeconds;

    uint32_t stepsToRun = static_cast<uint32_t>(cameraFixedAccumulatorSeconds / CAMERA_FIXED_STEP_SECONDS);
    stepsToRun = stepsToRun > CAMERA_MAX_FIXED_STEPS ? CAMERA_MAX_FIXED_STEPS : stepsToRun;
    const uint32_t safeSteps = stepsToRun == 0u ? 1u : stepsToRun;
    const double invSteps = 1.0 / static_cast<double>(safeSteps);
    const float mouseStepX = static_cast<float>(accumulatedMouseDeltaX * invSteps);
    const float mouseStepY = static_cast<float>(accumulatedMouseDeltaY * invSteps);
    const double consumeMask = stepsToRun == 0u ? 0.0 : 1.0;
    accumulatedMouseDeltaX *= (1.0 - consumeMask);
    accumulatedMouseDeltaY *= (1.0 - consumeMask);

    const float moveForward = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ? 1.0f : 0.0f;
    const float moveBack = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS ? 1.0f : 0.0f;
    const float moveRight = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ? 1.0f : 0.0f;
    const float moveLeft = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ? 1.0f : 0.0f;
    const float moveUp = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS ? 1.0f : 0.0f;
    const float moveDown = glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS ? 1.0f : 0.0f;
    const bool speedBoostHeld = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
    const float moveFB = moveForward - moveBack;
    const float moveRL = moveRight - moveLeft;
    const float moveUD = moveUp - moveDown;

    const float speedBoost = speedBoostHeld ? CAMERA_SPEED_BOOST_MULTIPLIER : 1.0f;
    const float step = CAMERA_MOVE_SPEED * speedBoost * static_cast<float>(CAMERA_FIXED_STEP_SECONDS);

    constexpr float maxPitch = 1.5533430f;

    for (uint32_t i = 0; i < stepsToRun; i++)
    {
        cameraYaw += mouseStepX * CAMERA_MOUSE_SENSITIVITY;
        cameraPitch -= mouseStepY * CAMERA_MOUSE_SENSITIVITY;
        cameraPitch = std::fmax(-maxPitch, std::fmin(cameraPitch, maxPitch));

        const float cosPitch = std::cos(cameraPitch);
        const float sinPitch = std::sin(cameraPitch);
        const float cosYaw = std::cos(cameraYaw);
        const float sinYaw = std::sin(cameraYaw);

        const float forwardX = cosPitch * cosYaw;
        const float forwardY = sinPitch;
        const float forwardZ = cosPitch * sinYaw;

        const float rightX = -sinYaw;
        const float rightY = 0.0f;
        const float rightZ = cosYaw;

        cameraPosX += (forwardX * moveFB + rightX * moveRL) * step;
        cameraPosY += (forwardY * moveFB + rightY * moveRL + moveUD) * step;
        cameraPosZ += (forwardZ * moveFB + rightZ * moveRL) * step;
    }

    cameraFixedAccumulatorSeconds -= static_cast<double>(stepsToRun) * CAMERA_FIXED_STEP_SECONDS;
    const double maxAccumulator = CAMERA_FIXED_STEP_SECONDS * static_cast<double>(CAMERA_MAX_FIXED_STEPS);
    cameraFixedAccumulatorSeconds = std::fmax(0.0, std::fmin(cameraFixedAccumulatorSeconds, maxAccumulator));
}

void WriteArenaHeaderData(uint32_t currentFrame)
{
    const uint32_t base = currentFrame * SLOT_WORDS;
    const float cosPitch = std::cos(cameraPitch);
    const float sinPitch = std::sin(cameraPitch);
    const float cosYaw = std::cos(cameraYaw);
    const float sinYaw = std::sin(cameraYaw);
    const float forwardX = cosPitch * cosYaw;
    const float forwardY = sinPitch;
    const float forwardZ = cosPitch * sinYaw;
    const float rightX = -sinYaw;
    const float rightY = 0.0f;
    const float rightZ = cosYaw;
    const float upX = -sinPitch * cosYaw;
    const float upY = cosPitch;
    const float upZ = -sinPitch * sinYaw;
    const float tanHalfFovY = std::tan(CAMERA_FOV_Y * 0.5f);

    dataBufferWords[base + HDR_CAM_POS_X] = std::bit_cast<uint32_t>(cameraPosX);
    dataBufferWords[base + HDR_CAM_POS_Y] = std::bit_cast<uint32_t>(cameraPosY);
    dataBufferWords[base + HDR_CAM_POS_Z] = std::bit_cast<uint32_t>(cameraPosZ);
    dataBufferWords[base + HDR_CAM_YAW] = std::bit_cast<uint32_t>(cameraYaw);
    dataBufferWords[base + HDR_CAM_PITCH] = std::bit_cast<uint32_t>(cameraPitch);
    dataBufferWords[base + HDR_CAM_MOVE_SPEED] = std::bit_cast<uint32_t>(CAMERA_MOVE_SPEED);
    dataBufferWords[base + HDR_CAM_MOUSE_SENSITIVITY] = std::bit_cast<uint32_t>(CAMERA_MOUSE_SENSITIVITY);
    dataBufferWords[base + HDR_CAM_FRAME_INDEX] = frameCounter++;
    dataBufferWords[base + HDR_CAM_FOV_Y] = std::bit_cast<uint32_t>(CAMERA_FOV_Y);

    dataBufferWords[base + HDR_GRID_MIN_X] = std::bit_cast<uint32_t>(SCENE_GRID_MIN_X);
    dataBufferWords[base + HDR_GRID_MIN_Y] = std::bit_cast<uint32_t>(SCENE_GRID_MIN_Y);
    dataBufferWords[base + HDR_GRID_MIN_Z] = std::bit_cast<uint32_t>(SCENE_GRID_MIN_Z);
    dataBufferWords[base + HDR_GRID_DIM_X] = SCENE_GRID_DIM_X;
    dataBufferWords[base + HDR_GRID_DIM_Y] = SCENE_GRID_DIM_Y;
    dataBufferWords[base + HDR_GRID_DIM_Z] = SCENE_GRID_DIM_Z;
    dataBufferWords[base + HDR_BRICK_COUNT] = sceneBrickCount;
    dataBufferWords[base + HDR_BRICK_TABLE_OFFSET_WORDS] = ARENA_BRICK_TABLE_BASE_WORD;
    dataBufferWords[base + HDR_BRICK_POOL_OFFSET_WORDS] = ARENA_BRICK_POOL_BASE_WORD;
    dataBufferWords[base + HDR_CAM_FORWARD_X] = std::bit_cast<uint32_t>(forwardX);
    dataBufferWords[base + HDR_CAM_FORWARD_Y] = std::bit_cast<uint32_t>(forwardY);
    dataBufferWords[base + HDR_CAM_FORWARD_Z] = std::bit_cast<uint32_t>(forwardZ);
    dataBufferWords[base + HDR_CAM_RIGHT_X] = std::bit_cast<uint32_t>(rightX);
    dataBufferWords[base + HDR_CAM_RIGHT_Y] = std::bit_cast<uint32_t>(rightY);
    dataBufferWords[base + HDR_CAM_RIGHT_Z] = std::bit_cast<uint32_t>(rightZ);
    dataBufferWords[base + HDR_CAM_UP_X] = std::bit_cast<uint32_t>(upX);
    dataBufferWords[base + HDR_CAM_UP_Y] = std::bit_cast<uint32_t>(upY);
    dataBufferWords[base + HDR_CAM_UP_Z] = std::bit_cast<uint32_t>(upZ);
    dataBufferWords[base + HDR_CAM_TAN_HALF_FOV_Y] = std::bit_cast<uint32_t>(tanHalfFovY);
    dataBufferWords[base + HDR_BRICK_VOXEL_SIZE] = std::bit_cast<uint32_t>(TEST_BRICK_VOXEL_SIZE);
    dataBufferWords[base + HDR_MACRO_MASK_OFFSET_WORDS] = ARENA_MACRO_MASK_BASE_WORD;
}

void WriteBrickData(uint32_t currentFrame)
{
    const uint32_t frameBase = currentFrame * SLOT_WORDS;
    const uint32_t tableBase = frameBase + ARENA_BRICK_TABLE_BASE_WORD;
    const uint32_t macroBase = frameBase + ARENA_MACRO_MASK_BASE_WORD;
    const uint32_t poolBase = frameBase + ARENA_BRICK_POOL_BASE_WORD;

    for (uint32_t i = 0; i < BRICK_TABLE_WORDS; i++)
    {
        dataBufferWords[tableBase + i] = EMPTY_BRICK_SLOT;
    }
    for (uint32_t i = 0; i < MACRO_MASK_WORDS; i++)
    {
        dataBufferWords[macroBase + i] = 0u;
    }

    uint32_t brickIndex = 0;
    for (uint32_t gz = 0; gz < SCENE_GRID_DIM_Z; gz++)
    {
        for (uint32_t gy = 0; gy < SCENE_GRID_DIM_Y; gy++)
        {
            for (uint32_t gx = 0; gx < SCENE_GRID_DIM_X; gx++)
            {
                const uint32_t gridIndex = GridLinearIndex(gx, gy, gz);
                const float brickMinX = SCENE_GRID_MIN_X + static_cast<float>(gx) * BRICK_WORLD_SIZE;
                const float brickMinY = SCENE_GRID_MIN_Y + static_cast<float>(gy) * BRICK_WORLD_SIZE;
                const float brickMinZ = SCENE_GRID_MIN_Z + static_cast<float>(gz) * BRICK_WORLD_SIZE;
                const uint64_t occupancy = BuildTerrainBrickMask(brickMinX, brickMinY, brickMinZ);

                if (occupancy == 0ull)
                {
                    continue;
                }

                dataBufferWords[tableBase + gridIndex] = brickIndex;

                const uint32_t brickBase = poolBase + brickIndex * BRICK_WORDS;
                dataBufferWords[brickBase + 0] = static_cast<uint32_t>(occupancy & 0xFFFFFFFFull);
                dataBufferWords[brickBase + 1] = static_cast<uint32_t>(occupancy >> 32);

                const uint32_t mx = gx / MACRO_BRICK_DIM;
                const uint32_t my = gy / MACRO_BRICK_DIM;
                const uint32_t mz = gz / MACRO_BRICK_DIM;
                const uint32_t macroIndex = MacroLinearIndex(mx, my, mz);
                const uint32_t localX = gx & (MACRO_BRICK_DIM - 1u);
                const uint32_t localY = gy & (MACRO_BRICK_DIM - 1u);
                const uint32_t localZ = gz & (MACRO_BRICK_DIM - 1u);
                const uint32_t macroBitIndex = BrickBitIndex(localX, localY, localZ);
                const uint32_t macroWordBase = macroBase + macroIndex * BRICK_WORDS;
                if (macroBitIndex < 32u)
                {
                    dataBufferWords[macroWordBase + 0] |= (1u << macroBitIndex);
                }
                else
                {
                    dataBufferWords[macroWordBase + 1] |= (1u << (macroBitIndex - 32u));
                }
                brickIndex++;
            }
        }
    }

    sceneBrickCount = brickIndex;
}

void RecordCommandBuffer(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet, uint32_t imageIndex, uint32_t currentFrame)
{
    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    const uint32_t headerBaseWord = currentFrame * SLOT_WORDS;
    vkCmdPushConstants(
        commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        ARENA_HEADER_WORDS * sizeof(uint32_t),
        dataBufferWords + headerBaseWord);

    if (!renderImageInitialized)
    {
        VkImageMemoryBarrier renderToGeneral{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = renderImage,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &renderToGeneral);
    }

    const uint32_t groupCountX = (swapExtent.width + 7) / 8;
    const uint32_t groupCountY = (swapExtent.height + 7) / 8;
    vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);

    VkImageMemoryBarrier renderToTransferSrc{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = renderImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &renderToTransferSrc);

    const VkImageLayout oldSwapLayout = (swapImagePresented[imageIndex] != 0u)
                                            ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                            : VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageMemoryBarrier swapToTransferDst{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = oldSwapLayout,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapImages[imageIndex],
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &swapToTransferDst);

    VkImageCopy copyRegion{
        .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .srcOffset = {0, 0, 0},
        .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .dstOffset = {0, 0, 0},
        .extent = {swapExtent.width, swapExtent.height, 1},
    };
    vkCmdCopyImage(
        commandBuffer,
        renderImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapImages[imageIndex],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copyRegion);

    VkImageMemoryBarrier swapToPresent{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapImages[imageIndex],
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &swapToPresent);

    VkImageMemoryBarrier renderBackToGeneral{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = renderImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &renderBackToGeneral);

    vkEndCommandBuffer(commandBuffer);
}

void DrawFrame(uint32_t currentFrame)
{
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    WriteArenaHeaderData(currentFrame);

    uint32_t imageIndex = 0;
    vkAcquireNextImageKHR(
        device,
        swapchain,
        UINT64_MAX,
        imageAvailableSemaphores[currentFrame],
        VK_NULL_HANDLE,
        &imageIndex);

    vkResetCommandBuffer(commandBuffers[currentFrame], 0);

    RecordCommandBuffer(commandBuffers[currentFrame], descriptorSets[currentFrame], imageIndex, currentFrame);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &imageAvailableSemaphores[currentFrame],
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffers[currentFrame],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &renderFinishedSemaphores[currentFrame],
    };

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]);
    renderImageInitialized = true;

    VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderFinishedSemaphores[currentFrame],
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &imageIndex,
    };

    vkQueuePresentKHR(graphicsQueue, &presentInfo);
    swapImagePresented[imageIndex] = 1u;
}

auto main() -> int
{
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window = glfwCreateWindow(static_cast<int>(WINDOW_WIDTH), static_cast<int>(WINDOW_HEIGHT), "greadbadbeyond", nullptr, nullptr);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported() == GLFW_TRUE)
    {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

    {
        uint32_t glfwExtensionCount = 0;
        const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::array<const char *, MAX_INSTANCE_EXTENSIONS> instanceExtensions{};
        uint32_t instanceExtensionCount = 0;

        for (uint32_t i = 0; i < glfwExtensionCount; i++)
        {
            instanceExtensions[instanceExtensionCount++] = glfwExtensions[i];
        }

        for (uint32_t i = 0; i < EXTRA_INSTANCE_EXTENSION_COUNT; i++)
        {
            instanceExtensions[instanceExtensionCount++] = EXTRA_INSTANCE_EXTENSIONS[i];
        }

#if defined(__APPLE__)
        constexpr uint32_t appApiVersion = VK_API_VERSION_1_1;
        constexpr VkInstanceCreateFlags instanceCreateFlags = PORTABILITY_ENUMERATE_FLAG;
#else
        constexpr uint32_t appApiVersion = VK_API_VERSION_1_3;
        constexpr VkInstanceCreateFlags instanceCreateFlags = 0;
#endif

        VkApplicationInfo appInfo{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "greadbadbeyond",
            .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .pEngineName = "greadbadbeyond",
            .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .apiVersion = appApiVersion,
        };

        VkInstanceCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .flags = instanceCreateFlags,
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = instanceExtensionCount,
            .ppEnabledExtensionNames = instanceExtensions.data(),
        };

        vkCreateInstance(&createInfo, nullptr, &instance);
    }

    glfwCreateWindowSurface(instance, window, nullptr, &surface);

    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

        std::array<VkPhysicalDevice, MAX_PHYSICAL_DEVICES> devices{};
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        physicalDevice = devices[0];
    }

    {
        std::array<const char *, MAX_DEVICE_EXTENSIONS> deviceExtensions{};
        uint32_t deviceExtensionCount = 0;
        deviceExtensions[deviceExtensionCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        for (uint32_t i = 0; i < EXTRA_DEVICE_EXTENSION_COUNT; i++)
        {
            deviceExtensions[deviceExtensionCount++] = EXTRA_DEVICE_EXTENSIONS[i];
        }

        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = 0,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        };

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkDeviceCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCreateInfo,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = deviceExtensionCount,
            .ppEnabledExtensionNames = deviceExtensions.data(),
            .pEnabledFeatures = &deviceFeatures,
        };

        vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
        vkGetDeviceQueue(device, 0, 0, &graphicsQueue);
    }

    {
        VkSurfaceCapabilitiesKHR surfaceCaps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps);

        uint32_t imageCount = surfaceCaps.minImageCount;
        if (imageCount < 2)
        {
            imageCount = 2;
        }

        swapExtent = surfaceCaps.currentExtent;

        VkSwapchainCreateInfoKHR createInfo{
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = surface,
            .minImageCount = imageCount,
            .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
            .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
            .imageExtent = swapExtent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .preTransform = surfaceCaps.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = VK_PRESENT_MODE_FIFO_KHR,
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE,
        };

        vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain);

        swapImageCount = MAX_SWAPCHAIN_IMAGES;
        vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, swapImages.data());

        for (uint32_t i = 0; i < swapImageCount; i++)
        {
            VkImageViewCreateInfo viewInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = swapImages[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = VK_FORMAT_B8G8R8A8_UNORM,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            };
            vkCreateImageView(device, &viewInfo, nullptr, &swapImageViews[i]);
        }
    }

    {
        VkImageCreateInfo imageInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .extent = {
                .width = swapExtent.width,
                .height = swapExtent.height,
                .depth = 1,
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        vkCreateImage(device, &imageInfo, nullptr, &renderImage);

        VkMemoryRequirements memReqs{};
        vkGetImageMemoryRequirements(device, renderImage, &memReqs);

        VkMemoryAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memReqs.size,
            .memoryTypeIndex = FindMemoryTypeIndex(
                memReqs.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        };
        vkAllocateMemory(device, &allocInfo, nullptr, &renderImageMemory);
        vkBindImageMemory(device, renderImage, renderImageMemory, 0);

        VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = renderImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        vkCreateImageView(device, &viewInfo, nullptr, &renderImageView);
    }

    {
        VkBufferCreateInfo bufferInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = DATA_BUFFER_SIZE,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        vkCreateBuffer(device, &bufferInfo, nullptr, &dataBuffer);

        VkMemoryRequirements memReqs{};
        vkGetBufferMemoryRequirements(device, dataBuffer, &memReqs);

        VkMemoryAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memReqs.size,
            .memoryTypeIndex = FindMemoryTypeIndex(
                memReqs.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        };
        vkAllocateMemory(device, &allocInfo, nullptr, &dataBufferMemory);
        vkBindBufferMemory(device, dataBuffer, dataBufferMemory, 0);
        void *mapped = nullptr;
        vkMapMemory(device, dataBufferMemory, 0, DATA_BUFFER_SIZE, 0, &mapped);
        dataBufferWords = static_cast<uint32_t *>(mapped);

        for (uint32_t i = 0; i < DATA_WORD_COUNT; i++)
        {
            dataBufferWords[i] = 0;
        }

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            WriteBrickData(i);
            WriteArenaHeaderData(i);
        }
    }

    {
        VkDescriptorSetLayoutBinding imageBinding{
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        };
        VkDescriptorSetLayoutBinding bufferBinding{
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        };
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{
            imageBinding,
            bufferBinding,
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 2,
            .pBindings = bindings.data(),
        };
        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout);

        VkPushConstantRange pushConstantRange{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = ARENA_HEADER_WORDS * static_cast<uint32_t>(sizeof(uint32_t)),
        };
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &descriptorSetLayout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pushConstantRange,
        };
        vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout);

        VkShaderModuleCreateInfo moduleInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = kTriangleCompSpv_size,
            .pCode = reinterpret_cast<const uint32_t *>(kTriangleCompSpv),
        };
        VkShaderModule computeModule = VK_NULL_HANDLE;
        vkCreateShaderModule(device, &moduleInfo, nullptr, &computeModule);

        VkPipelineShaderStageCreateInfo stageInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = computeModule,
            .pName = "main",
        };

        VkComputePipelineCreateInfo pipelineInfo{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = stageInfo,
            .layout = pipelineLayout,
        };
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline);

        vkDestroyShaderModule(device, computeModule, nullptr);

        VkDescriptorPoolSize imagePoolSize{
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = MAX_FRAMES_IN_FLIGHT,
        };
        VkDescriptorPoolSize bufferPoolSize{
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = MAX_FRAMES_IN_FLIGHT,
        };
        std::array<VkDescriptorPoolSize, 2> poolSizes{
            imagePoolSize,
            bufferPoolSize,
        };
        VkDescriptorPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = MAX_FRAMES_IN_FLIGHT,
            .poolSizeCount = 2,
            .pPoolSizes = poolSizes.data(),
        };
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);

        std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> setLayouts{};
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            setLayouts[i] = descriptorSetLayout;
        }

        VkDescriptorSetAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptorPool,
            .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
            .pSetLayouts = setLayouts.data(),
        };
        vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data());

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkDescriptorImageInfo imageInfo{
                .imageView = renderImageView,
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            };
            VkWriteDescriptorSet imageWrite{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSets[i],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo = &imageInfo,
                .pBufferInfo = nullptr,
                .pTexelBufferView = nullptr,
            };
            VkDescriptorBufferInfo bufferInfo{
                .buffer = dataBuffer,
                .offset = static_cast<VkDeviceSize>(i * SLOT_WORDS * sizeof(uint32_t)),
                .range = static_cast<VkDeviceSize>(SLOT_WORDS * sizeof(uint32_t)),
            };
            VkWriteDescriptorSet bufferWrite{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSets[i],
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pImageInfo = nullptr,
                .pBufferInfo = &bufferInfo,
                .pTexelBufferView = nullptr,
            };
            std::array<VkWriteDescriptorSet, 2> writes{
                imageWrite,
                bufferWrite,
            };
            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    {
        VkCommandPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = 0,
        };

        vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);

        VkCommandBufferAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
        };

        vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data());
    }

    {
        VkFenceCreateInfo fenceInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        VkSemaphoreCreateInfo semaphoreInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]);
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]);
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]);
        }
    }

    uint32_t currentFrame = 0;
    uint32_t fpsFrameCount = 0;
    double fpsWindowStart = glfwGetTime();
    while (glfwWindowShouldClose(window) == GLFW_FALSE)
    {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        UpdateFlightCamera();
        DrawFrame(currentFrame);
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

        fpsFrameCount++;
        const double now = glfwGetTime();
        const double elapsed = now - fpsWindowStart;
        if (elapsed >= 1.0)
        {
            const double fps = static_cast<double>(fpsFrameCount) / elapsed;
            char title[64]{};
            std::snprintf(title, sizeof(title), "greadbadbeyond | %.1f FPS", fps);
            glfwSetWindowTitle(window, title);
            fpsFrameCount = 0;
            fpsWindowStart = now;
        }
    }

    vkDeviceWaitIdle(device);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(device, inFlightFences[i], nullptr);
    }

    vkDestroyCommandPool(device, commandPool, nullptr);

    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    vkDestroyPipeline(device, computePipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

    for (uint32_t i = 0; i < swapImageCount; i++)
    {
        vkDestroyImageView(device, swapImageViews[i], nullptr);
    }

    vkDestroyImageView(device, renderImageView, nullptr);
    vkDestroyImage(device, renderImage, nullptr);
    vkFreeMemory(device, renderImageMemory, nullptr);

    vkUnmapMemory(device, dataBufferMemory);
    vkDestroyBuffer(device, dataBuffer, nullptr);
    vkFreeMemory(device, dataBufferMemory, nullptr);

    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    window = nullptr;

    glfwTerminate();
    return 0;
}
