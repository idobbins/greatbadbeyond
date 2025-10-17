#pragma once

#include <vulkan/vulkan.h>

typedef struct PCPush {
    uint32_t width;
    uint32_t height;
    uint32_t frame;
    uint32_t sphereCount;
    float camPos[3];
    float fovY;
    float camFwd[3];
    float _pad0;
    float camRight[3];
    float _pad1;
    float camUp[3];
    float _pad2;
    float worldMin[2];
    float worldMax[2];
    float groundY;
    float _pad3[3];
} PCPush;

void RtUpdateSpawnArea(void);
void RtRecordFrame(uint32_t imageIndex, VkExtent2D extent);
