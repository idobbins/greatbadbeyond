#pragma once

#include <stdint.h>
#include <vulkan/vulkan.h>

typedef struct PCPush {
    uint32_t width;
    uint32_t height;
    uint32_t frame;
    uint32_t sphereCount;
    uint32_t accumulationEpoch;
    float tanHalfFovY;
    float aspect;
    float _pad0;
    float camPos[3];
    float _pad1;
    float camFwd[3];
    float _pad2;
    float camRight[3];
    float _pad3;
    float camUp[3];
    float _pad4;
    float worldMin[2];
    float worldMax[2];
    float groundY;
    float _pad5[3];
    uint32_t gridDim[3];
    uint32_t showGrid;
    float gridMin[3];
    float _pad6;
    float gridInvCell[3];
    float _pad7;
} PCPush;

void RtUpdateSpawnArea(void);
void RtRecordFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex, VkExtent2D extent);
