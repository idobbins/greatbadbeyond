#include "runtime.h"
#include "vk_pipelines.h"
#include "rt_frame.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static void VulkanBuildShaderPath(const char *name, char *buffer, uint32_t capacity)
{
    Assert(name != NULL, "Shader name is null");
    Assert(buffer != NULL, "Shader path buffer is null");
    Assert(capacity > 0, "Shader path buffer capacity is zero");

    int written = snprintf(buffer, capacity, "%s/%s", VULKAN_SHADER_DIRECTORY, name);
    Assert(written > 0, "Failed to compose shader path");
    Assert((uint32_t)written < capacity, "Shader path buffer overflow");
}

static uint32_t VulkanReadBinaryFile(const char *path, uint8_t *buffer, uint32_t capacity)
{
    Assert(path != NULL, "File path is null");
    Assert(buffer != NULL, "File buffer is null");
    Assert(capacity > 0, "File buffer capacity is zero");

    FILE *file = fopen(path, "rb");
    Assert(file != NULL, "Failed to open file");

    int seekResult = fseek(file, 0, SEEK_END);
    Assert(seekResult == 0, "Failed to seek file end");
    long size = ftell(file);
    Assert(size >= 0, "Failed to query file size");
    Assert((uint32_t)size <= capacity, "File size exceeds buffer capacity");
    seekResult = fseek(file, 0, SEEK_SET);
    Assert(seekResult == 0, "Failed to seek file start");

    size_t readCount = fread(buffer, 1, (size_t)size, file);
    Assert(readCount == (size_t)size, "Failed to read file");

    int closeResult = fclose(file);
    Assert(closeResult == 0, "Failed to close file");

    return (uint32_t)size;
}

static VkShaderModule VulkanLoadShaderModule(const char *filename)
{
    Assert(GLOBAL.Vulkan.device != VK_NULL_HANDLE, "Vulkan device is not ready");

    char path[VULKAN_MAX_PATH_LENGTH];
    VulkanBuildShaderPath(filename, path, ARRAY_SIZE(path));

    uint8_t shaderData[VULKAN_MAX_SHADER_SIZE];
    uint32_t shaderSize = VulkanReadBinaryFile(path, shaderData, sizeof(shaderData));
    Assert(shaderSize > 0, "Shader file is empty");
    Assert((shaderSize % 4) == 0, "Shader file size is not aligned to 4 bytes");

    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shaderSize,
        .pCode = (const uint32_t *)shaderData,
    };

    VkShaderModule module = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(GLOBAL.Vulkan.device, &createInfo, NULL, &module);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan shader module");

    return module;
}

void LoadShaderModules(void)
{
    if ((GLOBAL.Vulkan.spheresInitSM != VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.primaryIntersectSM != VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.shadeShadowSM != VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.gridCountSM != VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.gridClassifySM != VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.gridScatterSM != VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.blitVertexShaderModule != VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.blitFragmentShaderModule != VK_NULL_HANDLE))
    {
        return;
    }

    GLOBAL.Vulkan.spheresInitSM = VulkanLoadShaderModule("spheres_init.spv");
    GLOBAL.Vulkan.primaryIntersectSM = VulkanLoadShaderModule("primary_intersect.spv");
    GLOBAL.Vulkan.shadeShadowSM = VulkanLoadShaderModule("shade_shadow.spv");
    GLOBAL.Vulkan.gridCountSM = VulkanLoadShaderModule("grid_count.spv");
    GLOBAL.Vulkan.gridClassifySM = VulkanLoadShaderModule("grid_classify.spv");
    GLOBAL.Vulkan.gridScatterSM = VulkanLoadShaderModule("grid_scatter.spv");
    GLOBAL.Vulkan.blitVertexShaderModule = VulkanLoadShaderModule("blit.vert.spv");
    GLOBAL.Vulkan.blitFragmentShaderModule = VulkanLoadShaderModule("blit.frag.spv");

    LogInfo("Vulkan shader modules ready");
}

void DestroyShaderModules(void)
{
    if (GLOBAL.Vulkan.spheresInitSM != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(GLOBAL.Vulkan.device, GLOBAL.Vulkan.spheresInitSM, NULL);
        GLOBAL.Vulkan.spheresInitSM = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.primaryIntersectSM != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(GLOBAL.Vulkan.device, GLOBAL.Vulkan.primaryIntersectSM, NULL);
        GLOBAL.Vulkan.primaryIntersectSM = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.shadeShadowSM != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(GLOBAL.Vulkan.device, GLOBAL.Vulkan.shadeShadowSM, NULL);
        GLOBAL.Vulkan.shadeShadowSM = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.gridCountSM != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(GLOBAL.Vulkan.device, GLOBAL.Vulkan.gridCountSM, NULL);
        GLOBAL.Vulkan.gridCountSM = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.gridClassifySM != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(GLOBAL.Vulkan.device, GLOBAL.Vulkan.gridClassifySM, NULL);
        GLOBAL.Vulkan.gridClassifySM = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.gridScatterSM != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(GLOBAL.Vulkan.device, GLOBAL.Vulkan.gridScatterSM, NULL);
        GLOBAL.Vulkan.gridScatterSM = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.blitVertexShaderModule != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(GLOBAL.Vulkan.device, GLOBAL.Vulkan.blitVertexShaderModule, NULL);
        GLOBAL.Vulkan.blitVertexShaderModule = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.blitFragmentShaderModule != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(GLOBAL.Vulkan.device, GLOBAL.Vulkan.blitFragmentShaderModule, NULL);
        GLOBAL.Vulkan.blitFragmentShaderModule = VK_NULL_HANDLE;
    }
}

static void EnsureComputePipelineLayout(void)
{
    if (GLOBAL.Vulkan.computePipelineLayout != VK_NULL_HANDLE)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.descriptorSetLayout != VK_NULL_HANDLE, "Descriptor set layout is not ready");

    VkPushConstantRange pushRange = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(PCPush),
    };

    VkPipelineLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &GLOBAL.Vulkan.descriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange,
    };

    VkResult layoutResult = vkCreatePipelineLayout(GLOBAL.Vulkan.device, &layoutInfo, NULL, &GLOBAL.Vulkan.computePipelineLayout);
    Assert(layoutResult == VK_SUCCESS, "Failed to create Vulkan compute pipeline layout");
}

static void EnsureBlitPipelineLayout(void)
{
    if (GLOBAL.Vulkan.blitPipelineLayout != VK_NULL_HANDLE)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.descriptorSetLayout != VK_NULL_HANDLE, "Descriptor set layout is not ready");

    VkPipelineLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &GLOBAL.Vulkan.descriptorSetLayout,
    };

    VkResult layoutResult = vkCreatePipelineLayout(GLOBAL.Vulkan.device, &layoutInfo, NULL, &GLOBAL.Vulkan.blitPipelineLayout);
    Assert(layoutResult == VK_SUCCESS, "Failed to create Vulkan blit pipeline layout");
}

void CreateComputePipelines(void)
{
    const bool ready =
        (GLOBAL.Vulkan.spheresInitPipe != VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.primaryIntersectPipe != VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.shadeShadowPipe != VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.gridCountPipe != VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.gridClassifyPipe != VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.gridScatterPipe != VK_NULL_HANDLE);
    if (ready)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.spheresInitSM != VK_NULL_HANDLE, "Spheres init shader module is not ready");
    Assert(GLOBAL.Vulkan.primaryIntersectSM != VK_NULL_HANDLE, "Primary intersect shader module is not ready");
    Assert(GLOBAL.Vulkan.shadeShadowSM != VK_NULL_HANDLE, "Shade shadow shader module is not ready");
    Assert(GLOBAL.Vulkan.gridCountSM != VK_NULL_HANDLE, "Grid count shader module is not ready");
    Assert(GLOBAL.Vulkan.gridClassifySM != VK_NULL_HANDLE, "Grid classify shader module is not ready");
    Assert(GLOBAL.Vulkan.gridScatterSM != VK_NULL_HANDLE, "Grid scatter shader module is not ready");

    EnsureComputePipelineLayout();

    if (GLOBAL.Vulkan.spheresInitPipe == VK_NULL_HANDLE)
    {
        VkPipelineShaderStageCreateInfo stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = GLOBAL.Vulkan.spheresInitSM,
            .pName = "main",
        };

        VkComputePipelineCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = stage,
            .layout = GLOBAL.Vulkan.computePipelineLayout,
        };

        VkResult result = vkCreateComputePipelines(GLOBAL.Vulkan.device, VK_NULL_HANDLE, 1, &info, NULL, &GLOBAL.Vulkan.spheresInitPipe);
        Assert(result == VK_SUCCESS, "Failed to create spheres init pipeline");
    }

    if (GLOBAL.Vulkan.primaryIntersectPipe == VK_NULL_HANDLE)
    {
        VkPipelineShaderStageCreateInfo stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = GLOBAL.Vulkan.primaryIntersectSM,
            .pName = "main",
        };

        VkComputePipelineCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = stage,
            .layout = GLOBAL.Vulkan.computePipelineLayout,
        };

        VkResult result = vkCreateComputePipelines(GLOBAL.Vulkan.device, VK_NULL_HANDLE, 1, &info, NULL, &GLOBAL.Vulkan.primaryIntersectPipe);
        Assert(result == VK_SUCCESS, "Failed to create primary intersect pipeline");
    }

    if (GLOBAL.Vulkan.shadeShadowPipe == VK_NULL_HANDLE)
    {
        VkPipelineShaderStageCreateInfo stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = GLOBAL.Vulkan.shadeShadowSM,
            .pName = "main",
        };

        VkComputePipelineCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = stage,
            .layout = GLOBAL.Vulkan.computePipelineLayout,
        };

        VkResult result = vkCreateComputePipelines(GLOBAL.Vulkan.device, VK_NULL_HANDLE, 1, &info, NULL, &GLOBAL.Vulkan.shadeShadowPipe);
        Assert(result == VK_SUCCESS, "Failed to create shade shadow pipeline");
    }

    if (GLOBAL.Vulkan.gridCountPipe == VK_NULL_HANDLE)
    {
        VkPipelineShaderStageCreateInfo stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = GLOBAL.Vulkan.gridCountSM,
            .pName = "main",
        };

        VkComputePipelineCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = stage,
            .layout = GLOBAL.Vulkan.computePipelineLayout,
        };

        VkResult result = vkCreateComputePipelines(GLOBAL.Vulkan.device, VK_NULL_HANDLE, 1, &info, NULL, &GLOBAL.Vulkan.gridCountPipe);
        Assert(result == VK_SUCCESS, "Failed to create grid count pipeline");
    }

    if (GLOBAL.Vulkan.gridClassifyPipe == VK_NULL_HANDLE)
    {
        VkPipelineShaderStageCreateInfo stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = GLOBAL.Vulkan.gridClassifySM,
            .pName = "main",
        };

        VkComputePipelineCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = stage,
            .layout = GLOBAL.Vulkan.computePipelineLayout,
        };

        VkResult result = vkCreateComputePipelines(GLOBAL.Vulkan.device, VK_NULL_HANDLE, 1, &info, NULL, &GLOBAL.Vulkan.gridClassifyPipe);
        Assert(result == VK_SUCCESS, "Failed to create grid classify pipeline");
    }

    if (GLOBAL.Vulkan.gridScatterPipe == VK_NULL_HANDLE)
    {
        VkPipelineShaderStageCreateInfo stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = GLOBAL.Vulkan.gridScatterSM,
            .pName = "main",
        };

        VkComputePipelineCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = stage,
            .layout = GLOBAL.Vulkan.computePipelineLayout,
        };

        VkResult result = vkCreateComputePipelines(GLOBAL.Vulkan.device, VK_NULL_HANDLE, 1, &info, NULL, &GLOBAL.Vulkan.gridScatterPipe);
        Assert(result == VK_SUCCESS, "Failed to create grid scatter pipeline");
    }

    LogInfo("Vulkan compute pipelines ready");
}

void CreateBlitPipeline(void)
{
    if (GLOBAL.Vulkan.blitPipeline != VK_NULL_HANDLE)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.blitVertexShaderModule != VK_NULL_HANDLE, "Vulkan blit vertex shader module is not ready");
    Assert(GLOBAL.Vulkan.blitFragmentShaderModule != VK_NULL_HANDLE, "Vulkan blit fragment shader module is not ready");
    Assert(GLOBAL.Vulkan.swapchainExtent.width > 0, "Swapchain extent width is zero");
    Assert(GLOBAL.Vulkan.swapchainExtent.height > 0, "Swapchain extent height is zero");

    EnsureBlitPipelineLayout();

    VkPipelineShaderStageCreateInfo shaderStages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = GLOBAL.Vulkan.blitVertexShaderModule,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = GLOBAL.Vulkan.blitFragmentShaderModule,
            .pName = "main",
        },
    };

    VkPipelineVertexInputStateCreateInfo vertexInput = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)GLOBAL.Vulkan.swapchainExtent.width,
        .height = (float)GLOBAL.Vulkan.swapchainExtent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    VkRect2D scissor = {
        .offset = (VkOffset2D){ 0, 0 },
        .extent = GLOBAL.Vulkan.swapchainExtent,
    };

    VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState colorAttachment = {
        .blendEnable = VK_FALSE,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo colorBlend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
    };

    VkPipelineRenderingCreateInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &GLOBAL.Vulkan.swapchainImageFormat,
    };

    VkGraphicsPipelineCreateInfo graphicsInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
        .stageCount = ARRAY_SIZE(shaderStages),
        .pStages = shaderStages,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = NULL,
        .pColorBlendState = &colorBlend,
        .pDynamicState = NULL,
        .layout = GLOBAL.Vulkan.blitPipelineLayout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
    };

    VkResult graphicsResult = vkCreateGraphicsPipelines(GLOBAL.Vulkan.device, VK_NULL_HANDLE, 1, &graphicsInfo, NULL, &GLOBAL.Vulkan.blitPipeline);
    Assert(graphicsResult == VK_SUCCESS, "Failed to create Vulkan blit pipeline");

    LogInfo("Vulkan blit pipeline ready");
}

void DestroyBlitPipeline(void)
{
    if (GLOBAL.Vulkan.blitPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(GLOBAL.Vulkan.device, GLOBAL.Vulkan.blitPipeline, NULL);
        GLOBAL.Vulkan.blitPipeline = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.blitPipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(GLOBAL.Vulkan.device, GLOBAL.Vulkan.blitPipelineLayout, NULL);
        GLOBAL.Vulkan.blitPipelineLayout = VK_NULL_HANDLE;
    }
}

void DestroyPipelines(void)
{
    if (GLOBAL.Vulkan.shadeShadowPipe != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(GLOBAL.Vulkan.device, GLOBAL.Vulkan.shadeShadowPipe, NULL);
        GLOBAL.Vulkan.shadeShadowPipe = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.primaryIntersectPipe != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(GLOBAL.Vulkan.device, GLOBAL.Vulkan.primaryIntersectPipe, NULL);
        GLOBAL.Vulkan.primaryIntersectPipe = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.spheresInitPipe != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(GLOBAL.Vulkan.device, GLOBAL.Vulkan.spheresInitPipe, NULL);
        GLOBAL.Vulkan.spheresInitPipe = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.gridScatterPipe != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(GLOBAL.Vulkan.device, GLOBAL.Vulkan.gridScatterPipe, NULL);
        GLOBAL.Vulkan.gridScatterPipe = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.gridClassifyPipe != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(GLOBAL.Vulkan.device, GLOBAL.Vulkan.gridClassifyPipe, NULL);
        GLOBAL.Vulkan.gridClassifyPipe = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.gridCountPipe != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(GLOBAL.Vulkan.device, GLOBAL.Vulkan.gridCountPipe, NULL);
        GLOBAL.Vulkan.gridCountPipe = VK_NULL_HANDLE;
    }

    DestroyBlitPipeline();

    if (GLOBAL.Vulkan.computePipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(GLOBAL.Vulkan.device, GLOBAL.Vulkan.computePipelineLayout, NULL);
        GLOBAL.Vulkan.computePipelineLayout = VK_NULL_HANDLE;
    }
}
