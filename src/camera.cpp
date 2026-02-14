#include <greadbadbeyond.h>
#include <utils.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

using namespace std;

static constexpr float Pi = 3.14159265358979323846f;
static constexpr float DefaultVerticalFovRadians = 17.0f * (Pi / 180.0f);
static constexpr float DefaultAperture = 0.0f;
static constexpr float DefaultFocusDistance = 1.0f;
static constexpr float DefaultDistanceFromOrigin = 10.0f;
static constexpr float DefaultTiltRadians = Pi * 0.25f; // 45 degrees down.
static constexpr float DefaultAzimuthRadians = Pi * 0.25f; // 45 degrees around Y.
static constexpr float MoveSpeed = 10.0f;
static constexpr float ZoomStep = 1.0f;
static constexpr float MaxDeltaSeconds = 0.05f;
static constexpr float Epsilon = 0.0001f;

struct CameraState
{
    bool ready;
    Vec3 position;
    Vec3 forward;
    Vec3 right;
    Vec3 up;
    float aperture;
    float focusDistance;
    float verticalFov;
};

static CameraState Camera = {
    .ready = false,
    .position = {0.0f, 0.0f, 0.0f},
    .forward = {0.0f, 0.0f, -1.0f},
    .right = {1.0f, 0.0f, 0.0f},
    .up = {0.0f, 1.0f, 0.0f},
    .aperture = DefaultAperture,
    .focusDistance = DefaultFocusDistance,
    .verticalFov = DefaultVerticalFovRadians,
};

void CreateCamera()
{
    if (Camera.ready)
    {
        return;
    }

    const float sinTilt = sinf(DefaultTiltRadians);
    const float cosTilt = cosf(DefaultTiltRadians);
    const float sinAzimuth = sinf(DefaultAzimuthRadians);
    const float cosAzimuth = cosf(DefaultAzimuthRadians);

    Camera.forward = {sinAzimuth * cosTilt, -sinTilt, -cosAzimuth * cosTilt};
    Camera.position = {
        -Camera.forward.x * DefaultDistanceFromOrigin,
        -Camera.forward.y * DefaultDistanceFromOrigin,
        -Camera.forward.z * DefaultDistanceFromOrigin,
    };

    const auto normalize = [](const Vec3 &v) -> Vec3
    {
        float len = sqrtf((v.x * v.x) + (v.y * v.y) + (v.z * v.z));
        if (len <= Epsilon)
        {
            return {0.0f, 0.0f, 0.0f};
        }
        float invLen = 1.0f / len;
        return {v.x * invLen, v.y * invLen, v.z * invLen};
    };
    const auto cross = [](const Vec3 &a, const Vec3 &b) -> Vec3
    {
        return {
            (a.y * b.z) - (a.z * b.y),
            (a.z * b.x) - (a.x * b.z),
            (a.x * b.y) - (a.y * b.x),
        };
    };

    Camera.forward = normalize(Camera.forward);
    Vec3 worldUp = {0.0f, 1.0f, 0.0f};
    Camera.right = normalize(cross(Camera.forward, worldUp));
    if (fabsf(Camera.right.x) <= Epsilon && fabsf(Camera.right.y) <= Epsilon && fabsf(Camera.right.z) <= Epsilon)
    {
        Camera.right = {1.0f, 0.0f, 0.0f};
    }

    Camera.up = normalize(cross(Camera.right, Camera.forward));
    if (fabsf(Camera.up.x) <= Epsilon && fabsf(Camera.up.y) <= Epsilon && fabsf(Camera.up.z) <= Epsilon)
    {
        Camera.up = worldUp;
    }

    Camera.aperture = DefaultAperture;
    Camera.focusDistance = DefaultFocusDistance;
    Camera.verticalFov = DefaultVerticalFovRadians;

    GLFWwindow *window = GetWindowHandle();
    if (window != nullptr)
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
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

    float dt = clamp(deltaSeconds, 0.0f, MaxDeltaSeconds);

    float moveX = 0.0f;
    float moveY = 0.0f;
    if ((glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS))
    {
        moveX -= 1.0f;
    }
    if ((glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS))
    {
        moveX += 1.0f;
    }
    if ((glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS))
    {
        moveY += 1.0f;
    }
    if ((glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS))
    {
        moveY -= 1.0f;
    }

    const auto normalize = [](const Vec3 &v) -> Vec3
    {
        float len = sqrtf((v.x * v.x) + (v.y * v.y) + (v.z * v.z));
        if (len <= Epsilon)
        {
            return {0.0f, 0.0f, 0.0f};
        }
        float invLen = 1.0f / len;
        return {v.x * invLen, v.y * invLen, v.z * invLen};
    };

    Vec3 planarForward = {Camera.forward.x, 0.0f, Camera.forward.z};
    Vec3 planarRight = {Camera.right.x, 0.0f, Camera.right.z};
    planarForward = normalize(planarForward);
    planarRight = normalize(planarRight);

    Vec3 move = {
        (planarRight.x * moveX) + (planarForward.x * moveY),
        0.0f,
        (planarRight.z * moveX) + (planarForward.z * moveY),
    };
    float moveLen = sqrtf((move.x * move.x) + (move.z * move.z));
    if (moveLen > 1.0f)
    {
        move.x /= moveLen;
        move.z /= moveLen;
    }

    bool changed = false;
    if (dt > 0.0f && (fabsf(move.x) > Epsilon || fabsf(move.z) > Epsilon))
    {
        Camera.position.x += move.x * MoveSpeed * dt;
        Camera.position.z += move.z * MoveSpeed * dt;
        changed = true;
    }

    float wheel = ConsumeMouseWheelDelta();
    if (fabsf(wheel) > Epsilon)
    {
        Camera.position.x += Camera.forward.x * (wheel * ZoomStep);
        Camera.position.y += Camera.forward.y * (wheel * ZoomStep);
        Camera.position.z += Camera.forward.z * (wheel * ZoomStep);
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
