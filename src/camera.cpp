#include <greadbadbeyond.h>
#include <utils.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

using namespace std;

static constexpr float Pi = 3.14159265358979323846f;
static constexpr float DefaultVerticalFovRadians = Pi / 3.0f; // 60 degrees
static constexpr float DefaultAperture = 0.0f;
static constexpr float DefaultFocusDistance = 1.0f;
static constexpr float MouseSensitivity = 0.0025f;
static constexpr float MoveSpeed = 5.0f;
static constexpr float MoveBoost = 3.0f;
static constexpr float PitchLimitRadians = Pi * 0.49f; // ~88 degrees

struct CameraState
{
    bool ready;
    Vec3 position;
    float yaw;
    float pitch;
    Vec3 forward;
    Vec3 right;
    Vec3 up;
    float aperture;
    float focusDistance;
    float verticalFov;
    bool cursorInitialized;
    double lastCursorX;
    double lastCursorY;
};

static CameraState Camera = {
    .ready = false,
    .position = {0.0f, 1.5f, 6.0f},
    .yaw = 0.0f,
    .pitch = 0.0f,
    .forward = {0.0f, 0.0f, -1.0f},
    .right = {1.0f, 0.0f, 0.0f},
    .up = {0.0f, 1.0f, 0.0f},
    .aperture = DefaultAperture,
    .focusDistance = DefaultFocusDistance,
    .verticalFov = DefaultVerticalFovRadians,
    .cursorInitialized = false,
    .lastCursorX = 0.0,
    .lastCursorY = 0.0,
};

static auto Add(const Vec3 &a, const Vec3 &b) -> Vec3
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

static auto Sub(const Vec3 &a, const Vec3 &b) -> Vec3
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

static auto Scale(const Vec3 &v, float s) -> Vec3
{
    return {v.x * s, v.y * s, v.z * s};
}

static auto Dot(const Vec3 &a, const Vec3 &b) -> float
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static auto Cross(const Vec3 &a, const Vec3 &b) -> Vec3
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

static auto Length(const Vec3 &v) -> float
{
    return sqrtf(max(Dot(v, v), 0.0f));
}

static auto Normalize(const Vec3 &v) -> Vec3
{
    float len = Length(v);
    if (len <= 0.0f)
    {
        return {0.0f, 0.0f, 0.0f};
    }
    float invLen = 1.0f / len;
    return {v.x * invLen, v.y * invLen, v.z * invLen};
}

static void UpdateBasis()
{
    float cosPitch = cosf(Camera.pitch);
    float sinPitch = sinf(Camera.pitch);
    float cosYaw = cosf(Camera.yaw);
    float sinYaw = sinf(Camera.yaw);

    Vec3 forward = {
        cosPitch * sinYaw,
        sinPitch,
        cosPitch * cosYaw * -1.0f, // face -Z when yaw=0
    };
    forward = Normalize(forward);
    if (Length(forward) <= 0.0f)
    {
        forward = {0.0f, 0.0f, -1.0f};
    }

    Vec3 worldUp = {0.0f, 1.0f, 0.0f};
    Vec3 right = Normalize(Cross(forward, worldUp));
    if (Length(right) <= 0.0f)
    {
        right = {1.0f, 0.0f, 0.0f};
    }

    Vec3 up = Normalize(Cross(right, forward));
    if (Length(up) <= 0.0f)
    {
        up = worldUp;
    }

    Camera.forward = forward;
    Camera.right = right;
    Camera.up = up;
}

void CreateCamera()
{
    if (Camera.ready)
    {
        return;
    }

    Camera.position = {0.0f, 1.5f, 6.0f};
    Camera.yaw = 0.0f;
    Camera.pitch = 0.0f;
    Camera.aperture = DefaultAperture;
    Camera.focusDistance = DefaultFocusDistance;
    Camera.verticalFov = DefaultVerticalFovRadians;
    Camera.cursorInitialized = false;

    UpdateBasis();

    GLFWwindow *window = GetWindowHandle();
    if (window != nullptr)
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    Camera.ready = true;
}

void DestroyCamera()
{
    if (!Camera.ready)
    {
        return;
    }

    GLFWwindow *window = GetWindowHandle();
    if (window != nullptr)
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    Camera.ready = false;
    Camera.cursorInitialized = false;
}

void UpdateCameraFromInput(float deltaSeconds)
{
    if (!Camera.ready)
    {
        return;
    }

    GLFWwindow *window = GetWindowHandle();
    if (window == nullptr)
    {
        return;
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window, &cursorX, &cursorY);

    if (!Camera.cursorInitialized)
    {
        Camera.lastCursorX = cursorX;
        Camera.lastCursorY = cursorY;
        Camera.cursorInitialized = true;
    }

    double deltaX = cursorX - Camera.lastCursorX;
    double deltaY = cursorY - Camera.lastCursorY;
    Camera.lastCursorX = cursorX;
    Camera.lastCursorY = cursorY;

    bool changed = false;

    if ((deltaX != 0.0) || (deltaY != 0.0))
    {
        Camera.yaw += static_cast<float>(deltaX) * MouseSensitivity;
        Camera.pitch -= static_cast<float>(deltaY) * MouseSensitivity;
        Camera.pitch = clamp(Camera.pitch, -PitchLimitRadians, PitchLimitRadians);
        changed = true;
    }

    UpdateBasis();

    Vec3 move = {0.0f, 0.0f, 0.0f};
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
    {
        move = Add(move, Camera.forward);
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
    {
        move = Sub(move, Camera.forward);
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
    {
        move = Sub(move, Camera.right);
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
    {
        move = Add(move, Camera.right);
    }

    Vec3 worldUp = {0.0f, 1.0f, 0.0f};
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
    {
        move = Add(move, worldUp);
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
    {
        move = Sub(move, worldUp);
    }

    float speed = MoveSpeed;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
    {
        speed *= MoveBoost;
    }

    float dt = max(deltaSeconds, 0.0f);
    if (Length(move) > 0.0f && dt > 0.0f)
    {
        Vec3 dir = Normalize(move);
        Camera.position = Add(Camera.position, Scale(dir, speed * dt));
        changed = true;
    }

    if (changed)
    {
        ResetCameraAccum();
    }
}

auto GetCameraParams() -> CameraParams
{
    CameraParams params = {};
    params.position = Camera.position;
    params.verticalFovRadians = Camera.verticalFov;
    params.forward = Camera.forward;
    params.aperture = Camera.aperture;
    params.right = Camera.right;
    params.focusDistance = Camera.focusDistance;
    params.up = Camera.up;
    params.pad3 = 0.0f;
    return params;
}
