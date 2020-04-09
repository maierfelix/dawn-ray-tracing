// Copyright 2017 The Dawn Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "SampleUtils.h"
#include "utils/SystemUtils.h"
#include "utils/WGPUHelpers.h"

WGPUDevice device;
WGPUQueue queue;
WGPUSwapChain swapchain;

WGPURenderPipeline pipeline;
WGPUBindGroupLayout bindGroupLayout;
WGPUBindGroup bindGroup;

WGPUBuffer vertexBuffer;
WGPUBuffer indexBuffer;

WGPUBuffer pixelBuffer;
WGPUBuffer cameraBuffer;

WGPUShaderModule vsModule;
WGPUShaderModule fsModule;
WGPUShaderModule rayGenModule;
WGPUShaderModule rayCHitModule;
WGPUShaderModule rayMissModule;

WGPUTextureFormat swapChainFormat;

WGPURayTracingAccelerationContainer geometryContainer;
WGPURayTracingAccelerationContainer instanceContainer;

WGPUBindGroupLayout rtBindGroupLayout;
WGPUBindGroup rtBindGroup;

WGPUPipelineLayout rtPipelineLayout;
WGPURayTracingPipeline rtPipeline;

uint32_t width = 640;
uint32_t height = 480;

uint64_t pixelBufferSize = width * height * 4 * sizeof(float);

struct CameraData {
    glm::mat4 view;
    glm::mat4 projection;
};

void init() {
    device = CreateCppDawnDevice(wgpu::BackendType::D3D12).Release();
    queue = wgpuDeviceCreateQueue(device);

    {
        WGPUSwapChainDescriptor descriptor;
        descriptor.nextInChain = nullptr;
        descriptor.label = nullptr;
        descriptor.implementation = GetSwapChainImplementation();
        swapchain = wgpuDeviceCreateSwapChain(device, nullptr, &descriptor);
    }
    swapChainFormat = static_cast<WGPUTextureFormat>(GetPreferredSwapChainTextureFormat());
    wgpuSwapChainConfigure(swapchain, swapChainFormat, WGPUTextureUsage_OutputAttachment, width,
                           height);

    const char* rayGen = R"(
        #version 460
        #extension GL_NV_ray_tracing : require

        layout(location = 0) rayPayloadNV vec3 hitValue;

        layout(binding = 0, set = 0) uniform accelerationStructureNV topLevelAS;

        layout(std140, set = 0, binding = 1) buffer PixelBuffer {
            vec4 pixels[];
        } pixelBuffer;

        layout(set = 0, binding = 2) uniform Camera {
            mat4 view;
            mat4 projection;
        } uCamera;

        void main() {
            ivec2 ipos = ivec2(gl_LaunchIDNV.xy);
            const ivec2 resolution = ivec2(gl_LaunchSizeNV.xy);

            const vec2 offset = vec2(0);
            const vec2 pixel = vec2(ipos.x, ipos.y);
            const vec2 uv = (pixel / gl_LaunchSizeNV.xy) * 2.0 - 1.0;

            vec4 origin = uCamera.view * vec4(offset, 0, 1);
            vec4 target = uCamera.projection * (vec4(uv.x, uv.y, 1, 1));
            vec4 direction = uCamera.view * vec4(normalize(target.xyz), 0);

            hitValue = vec3(0);
            traceNV(topLevelAS, gl_RayFlagsOpaqueNV, 0xFF, 0, 0, 0, origin.xyz, 0.01, direction.xyz, 4096.0, 0);

            const uint pixelIndex = ipos.y * resolution.x + ipos.x;
            pixelBuffer.pixels[pixelIndex] = vec4(hitValue, 1);
        }
    )";

    const char* rayCHit = R"(
        #version 460
        #extension GL_NV_ray_tracing : require

        layout(location = 0) rayPayloadInNV vec3 hitValue;

        hitAttributeNV vec3 attribs;

        void main() {
            const vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
            hitValue = bary;
        }
    )";

    const char* rayMiss = R"(
        #version 460
        #extension GL_NV_ray_tracing : require

        layout(location = 0) rayPayloadInNV vec3 hitValue;

        void main() {
            hitValue = vec3(0.15);
        }
    )";

    const char* vs = R"(
        #version 460

        layout (location = 0) out vec2 uv;

        void main() {
            vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
            gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
            uv = pos;
        }
    )";

    const char* fs = R"(
        #version 460

        layout (location = 0) in vec2 uv;
        layout (location = 0) out vec4 outColor;

        layout(std140, set = 0, binding = 0) buffer PixelBuffer {
            vec4 pixels[];
        } pixelBuffer;

        const vec2 resolution = vec2(640, 480);

        void main() {
            const ivec2 bufferCoord = ivec2(floor(uv * resolution));
            const vec2 fragCoord = (uv * resolution);
            const uint pixelIndex = bufferCoord.y * uint(resolution.x) + bufferCoord.x;

            vec4 pixelColor = pixelBuffer.pixels[pixelIndex];
            outColor = pixelColor;
        }
    )";

    vsModule = utils::CreateShaderModule(wgpu::Device(device), utils::SingleShaderStage::Vertex, vs)
                   .Release();

    fsModule = utils::CreateShaderModule(device, utils::SingleShaderStage::Fragment, fs).Release();

    rayGenModule =
        utils::CreateShaderModule(device, utils::SingleShaderStage::RayGeneration, rayGen)
            .Release();

    rayCHitModule =
        utils::CreateShaderModule(device, utils::SingleShaderStage::RayClosestHit, rayCHit)
            .Release();

    rayMissModule =
        utils::CreateShaderModule(device, utils::SingleShaderStage::RayMiss, rayMiss).Release();

    // clang-format off
    const float vertexData[] = {
         1.0f,  1.0f,  0.0f,
        -1.0f,  1.0f,  0.0f,
         0.0f, -1.0f,  0.0f
    };
    // clang-format on
    {
        WGPUBufferDescriptor descriptor;
        descriptor.label = nullptr;
        descriptor.nextInChain = nullptr;
        descriptor.size = sizeof(vertexData);
        descriptor.usage = WGPUBufferUsage_CopyDst;

        vertexBuffer = wgpuDeviceCreateBuffer(device, &descriptor);
        wgpuBufferSetSubData(vertexBuffer, 0, sizeof(vertexData), vertexData);
    }

    // clang-format off
    const uint32_t indexData[] = {
        0, 1, 2
    };
    // clang-format on
    {
        WGPUBufferDescriptor descriptor;
        descriptor.label = nullptr;
        descriptor.nextInChain = nullptr;
        descriptor.size = sizeof(indexData);
        descriptor.usage = WGPUBufferUsage_CopyDst;

        indexBuffer = wgpuDeviceCreateBuffer(device, &descriptor);
        wgpuBufferSetSubData(indexBuffer, 0, sizeof(indexData), indexData);
    }

    {
        WGPUBufferDescriptor descriptor;
        descriptor.label = nullptr;
        descriptor.nextInChain = nullptr;
        descriptor.size = pixelBufferSize;
        descriptor.usage = WGPUBufferUsage_Storage;

        pixelBuffer = wgpuDeviceCreateBuffer(device, &descriptor);
    }

    {
        WGPUBufferDescriptor descriptor;
        descriptor.label = nullptr;
        descriptor.nextInChain = nullptr;
        descriptor.size = sizeof(CameraData);
        descriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;

        CameraData data = {};

        float aspect = width / height;
        data.projection = glm::perspective(2.0f * glm::pi<float>() / 5.0f, -aspect, 0.1f, 4096.0f);
        data.projection = glm::inverse(data.projection);
        data.projection[1][1] *= -1.0f;

        data.view = glm::translate(data.view, glm::vec3(0.0f, 0.0f, -2.0f));
        data.view = glm::inverse(data.view);

        cameraBuffer = wgpuDeviceCreateBuffer(device, &descriptor);
        wgpuBufferSetSubData(cameraBuffer, 0, sizeof(CameraData), &data);
    }

    {
        WGPURayTracingAccelerationGeometryVertexDescriptor vertexDescriptor;
        vertexDescriptor.offset = 0;
        vertexDescriptor.buffer = vertexBuffer;
        vertexDescriptor.format = WGPUVertexFormat_Float3;
        vertexDescriptor.stride = 3 * sizeof(float);
        vertexDescriptor.count = sizeof(vertexData) / sizeof(float);

        WGPURayTracingAccelerationGeometryIndexDescriptor indexDescriptor;
        indexDescriptor.offset = 0;
        indexDescriptor.buffer = indexBuffer;
        indexDescriptor.format = WGPUIndexFormat_Uint32;
        indexDescriptor.count = sizeof(indexData) / sizeof(uint32_t);

        WGPURayTracingAccelerationGeometryDescriptor geometry;
        geometry.flags = WGPURayTracingAccelerationGeometryFlag_Opaque;
        geometry.type = WGPURayTracingAccelerationGeometryType_Triangles;
        geometry.vertex = &vertexDescriptor;
        geometry.index = &indexDescriptor;
        geometry.aabb = nullptr;

        WGPURayTracingAccelerationContainerDescriptor descriptor;
        descriptor.level = WGPURayTracingAccelerationContainerLevel_Bottom;
        descriptor.flags = WGPURayTracingAccelerationContainerFlag_PreferFastTrace;
        descriptor.geometryCount = 1;
        descriptor.geometries = &geometry;
        descriptor.instanceCount = 0;
        descriptor.instances = nullptr;

        geometryContainer = wgpuDeviceCreateRayTracingAccelerationContainer(device, &descriptor);
    }

    {
        // clang-format off
        const float transformMatrix[] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f
		};
        // clang-format on

        WGPURayTracingAccelerationInstanceDescriptor instanceDescriptor;
        instanceDescriptor.flags = WGPURayTracingAccelerationInstanceFlag_TriangleCullDisable;
        instanceDescriptor.instanceId = 0;
        instanceDescriptor.instanceOffset = 0x0;
        instanceDescriptor.mask = 0xFF;
        instanceDescriptor.geometryContainer = geometryContainer;
        instanceDescriptor.transformMatrix = transformMatrix;
        instanceDescriptor.transformMatrixSize = 12;
        instanceDescriptor.transform = nullptr;

        WGPURayTracingAccelerationContainerDescriptor descriptor;
        descriptor.level = WGPURayTracingAccelerationContainerLevel_Top;
        descriptor.flags = WGPURayTracingAccelerationContainerFlag_PreferFastTrace;
        descriptor.geometryCount = 0;
        descriptor.geometries = nullptr;
        descriptor.instanceCount = 1;
        descriptor.instances = &instanceDescriptor;

        instanceContainer = wgpuDeviceCreateRayTracingAccelerationContainer(device, &descriptor);
    }

    {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
        wgpuCommandEncoderBuildRayTracingAccelerationContainer(encoder, instanceContainer);
        WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(queue, 1, &commandBuffer);

        wgpuCommandEncoderRelease(encoder);
        wgpuCommandBufferRelease(commandBuffer);
    }
    
    WGPURayTracingShaderBindingTable sbt;
    {
        // clang-format off
        WGPURayTracingShaderBindingTableStagesDescriptor stagesDescriptor[3] = {
            {WGPUShaderStage_RayGeneration, rayGenModule},
            {WGPUShaderStage_RayClosestHit, rayCHitModule},
            {WGPUShaderStage_RayMiss,       rayMissModule}
        };

        WGPURayTracingShaderBindingTableGroupsDescriptor groupsDescriptor[3] = {
            {WGPURayTracingShaderBindingTableGroupType_General, 0, -1, -1, -1},
            {WGPURayTracingShaderBindingTableGroupType_TrianglesHitGroup, -1, 1, -1, -1},
            {WGPURayTracingShaderBindingTableGroupType_General, 2, -1, -1, -1}
        };
        // clang-format on

        WGPURayTracingShaderBindingTableDescriptor descriptor{};
        descriptor.stagesCount = 3;
        descriptor.stages = stagesDescriptor;
        descriptor.groupsCount = 3;
        descriptor.groups = groupsDescriptor;

        sbt = wgpuDeviceCreateRayTracingShaderBindingTable(device, &descriptor);
    }

    {
        WGPUBindGroupLayoutBinding bindingDescriptors[3];
        // acceleration container
        bindingDescriptors[0] = {};
        bindingDescriptors[0].binding = 0;
        bindingDescriptors[0].type = WGPUBindingType_AccelerationContainer;
        bindingDescriptors[0].visibility = WGPUShaderStage_RayGeneration;
        // pixel buffer
        bindingDescriptors[1] = {};
        bindingDescriptors[1].binding = 1;
        bindingDescriptors[1].type = WGPUBindingType_StorageBuffer;
        bindingDescriptors[1].visibility = WGPUShaderStage_RayGeneration;
        // camera buffer
        bindingDescriptors[2] = {};
        bindingDescriptors[2].binding = 2;
        bindingDescriptors[2].type = WGPUBindingType_UniformBuffer;
        bindingDescriptors[2].visibility = WGPUShaderStage_RayGeneration;

        WGPUBindGroupLayoutDescriptor descriptor;
        descriptor.label = nullptr;
        descriptor.nextInChain = nullptr;
        descriptor.bindingCount = 3;
        descriptor.bindings = bindingDescriptors;

        rtBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &descriptor);
    }

    {
        WGPUBindGroupBinding bindingDescriptors[3];
        // acceleration container
        bindingDescriptors[0] = {};
        bindingDescriptors[0].binding = 0;
        bindingDescriptors[0].offset = 0;
        bindingDescriptors[0].size = 0;
        bindingDescriptors[0].buffer = nullptr;
        bindingDescriptors[0].sampler = nullptr;
        bindingDescriptors[0].textureView = nullptr;
        bindingDescriptors[0].accelerationContainer = instanceContainer;
        // storage buffer
        bindingDescriptors[1] = {};
        bindingDescriptors[1].binding = 1;
        bindingDescriptors[1].offset = 0;
        bindingDescriptors[1].size = pixelBufferSize;
        bindingDescriptors[1].buffer = pixelBuffer;
        bindingDescriptors[1].sampler = nullptr;
        bindingDescriptors[1].textureView = nullptr;
        bindingDescriptors[1].accelerationContainer = nullptr;
        // camera buffer
        bindingDescriptors[2] = {};
        bindingDescriptors[2].binding = 2;
        bindingDescriptors[2].offset = 0;
        bindingDescriptors[2].size = sizeof(CameraData);
        bindingDescriptors[2].buffer = cameraBuffer;
        bindingDescriptors[2].sampler = nullptr;
        bindingDescriptors[2].textureView = nullptr;
        bindingDescriptors[2].accelerationContainer = nullptr;

        WGPUBindGroupDescriptor descriptor;
        descriptor.label = nullptr;
        descriptor.nextInChain = nullptr;
        descriptor.layout = rtBindGroupLayout;
        descriptor.bindingCount = 3;
        descriptor.bindings = bindingDescriptors;

        rtBindGroup = wgpuDeviceCreateBindGroup(device, &descriptor);
    }

    printf("Bindgroup created!\n");

    /*
    {
        WGPUPipelineLayoutDescriptor descriptor;
        descriptor.label = nullptr;
        descriptor.nextInChain = nullptr;
        descriptor.bindGroupLayoutCount = 1;
        descriptor.bindGroupLayouts = &rtBindGroupLayout;

        rtPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &descriptor);
    }

    {
        WGPURayTracingStateDescriptor rtStateDescriptor;
        rtStateDescriptor.maxRecursionDepth = 1;
        rtStateDescriptor.shaderBindingTable = sbt;

        WGPURayTracingPipelineDescriptor descriptor;
        descriptor.label = nullptr;
        descriptor.layout = rtPipelineLayout;
        descriptor.rayTracingState = &rtStateDescriptor;

        rtPipeline = wgpuDeviceCreateRayTracingPipeline(device, &descriptor);
    }*/

    {
        WGPUBindGroupLayoutBinding bindingDescriptors[1];
        // pixel buffer
        bindingDescriptors[0] = {};
        bindingDescriptors[0].binding = 0;
        bindingDescriptors[0].type = WGPUBindingType_StorageBuffer;
        bindingDescriptors[0].visibility = WGPUShaderStage_Fragment;

        WGPUBindGroupLayoutDescriptor descriptor;
        descriptor.label = nullptr;
        descriptor.nextInChain = nullptr;
        descriptor.bindingCount = 1;
        descriptor.bindings = bindingDescriptors;

        bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &descriptor);
    }

    {
        WGPUBindGroupBinding bindingDescriptors[1];
        // storage buffer
        bindingDescriptors[0] = {};
        bindingDescriptors[0].binding = 0;
        bindingDescriptors[0].offset = 0;
        bindingDescriptors[0].size = pixelBufferSize;
        bindingDescriptors[0].buffer = pixelBuffer;
        bindingDescriptors[0].sampler = nullptr;
        bindingDescriptors[0].textureView = nullptr;
        bindingDescriptors[0].accelerationContainer = nullptr;

        WGPUBindGroupDescriptor descriptor;
        descriptor.label = nullptr;
        descriptor.nextInChain = nullptr;
        descriptor.layout = bindGroupLayout;
        descriptor.bindingCount = 1;
        descriptor.bindings = bindingDescriptors;

        bindGroup = wgpuDeviceCreateBindGroup(device, &descriptor);
    }

    {
        WGPURenderPipelineDescriptor descriptor;
        descriptor.label = nullptr;
        descriptor.nextInChain = nullptr;

        descriptor.vertexStage.nextInChain = nullptr;
        descriptor.vertexStage.module = vsModule;
        descriptor.vertexStage.entryPoint = "main";

        WGPUProgrammableStageDescriptor fragmentStage;
        fragmentStage.nextInChain = nullptr;
        fragmentStage.module = fsModule;
        fragmentStage.entryPoint = "main";
        descriptor.fragmentStage = &fragmentStage;

        descriptor.sampleCount = 1;

        WGPUBlendDescriptor blendDescriptor;
        blendDescriptor.operation = WGPUBlendOperation_Add;
        blendDescriptor.srcFactor = WGPUBlendFactor_One;
        blendDescriptor.dstFactor = WGPUBlendFactor_One;
        WGPUColorStateDescriptor colorStateDescriptor;
        colorStateDescriptor.nextInChain = nullptr;
        colorStateDescriptor.format = swapChainFormat;
        colorStateDescriptor.alphaBlend = blendDescriptor;
        colorStateDescriptor.colorBlend = blendDescriptor;
        colorStateDescriptor.writeMask = WGPUColorWriteMask_All;

        descriptor.colorStateCount = 1;
        descriptor.colorStates = &colorStateDescriptor;

        WGPUPipelineLayoutDescriptor pl;
        pl.nextInChain = nullptr;
        pl.label = nullptr;
        pl.bindGroupLayoutCount = 1;
        pl.bindGroupLayouts = &bindGroupLayout;
        descriptor.layout = wgpuDeviceCreatePipelineLayout(device, &pl);

        WGPUVertexStateDescriptor vertexState;
        vertexState.nextInChain = nullptr;
        vertexState.indexFormat = WGPUIndexFormat_Uint32;
        vertexState.vertexBufferCount = 0;
        vertexState.vertexBuffers = nullptr;
        descriptor.vertexState = &vertexState;

        WGPURasterizationStateDescriptor rasterizationState;
        rasterizationState.nextInChain = nullptr;
        rasterizationState.frontFace = WGPUFrontFace_CCW;
        rasterizationState.cullMode = WGPUCullMode_None;
        rasterizationState.depthBias = 0;
        rasterizationState.depthBiasSlopeScale = 0.0;
        rasterizationState.depthBiasClamp = 0.0;
        descriptor.rasterizationState = &rasterizationState;

        descriptor.primitiveTopology = WGPUPrimitiveTopology_TriangleList;
        descriptor.sampleMask = 0xFFFFFFFF;
        descriptor.alphaToCoverageEnabled = false;

        descriptor.depthStencilState = nullptr;

        pipeline = wgpuDeviceCreateRenderPipeline(device, &descriptor);
    }

    wgpuShaderModuleRelease(vsModule);
    wgpuShaderModuleRelease(fsModule);
}

void frame() {
    WGPUTextureView backbufferView = wgpuSwapChainGetCurrentTextureView(swapchain);

    /*{
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);

        WGPURayTracingPassDescriptor rayTracingPassInfo;
        rayTracingPassInfo.label = nullptr;
        rayTracingPassInfo.nextInChain = nullptr;

        WGPURayTracingPassEncoder pass =
            wgpuCommandEncoderBeginRayTracingPass(encoder, &rayTracingPassInfo);
        wgpuRayTracingPassEncoderSetPipeline(pass, rtPipeline);
        wgpuRayTracingPassEncoderSetBindGroup(pass, 0, rtBindGroup, 0, nullptr);
        wgpuRayTracingPassEncoderTraceRays(pass, 0, 1, 2, width, height, 1);
        wgpuRayTracingPassEncoderEndPass(pass);
        wgpuRayTracingPassEncoderRelease(pass);

        WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuCommandEncoderRelease(encoder);
        wgpuQueueSubmit(queue, 1, &commandBuffer);
        wgpuCommandBufferRelease(commandBuffer);
    }*/

    {
        WGPURenderPassDescriptor renderpassInfo;
        renderpassInfo.nextInChain = nullptr;
        renderpassInfo.label = nullptr;

        WGPURenderPassColorAttachmentDescriptor colorAttachment;
        {
            colorAttachment.attachment = backbufferView;
            colorAttachment.resolveTarget = nullptr;
            colorAttachment.clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            renderpassInfo.colorAttachmentCount = 1;
            renderpassInfo.colorAttachments = &colorAttachment;
            renderpassInfo.depthStencilAttachment = nullptr;
        }

        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderpassInfo);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEndPass(pass);
        wgpuRenderPassEncoderRelease(pass);

        WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuCommandEncoderRelease(encoder);
        wgpuQueueSubmit(queue, 1, &commandBuffer);
        wgpuCommandBufferRelease(commandBuffer);
    }

    wgpuSwapChainPresent(swapchain);
    wgpuTextureViewRelease(backbufferView);

    DoFlush();
}

int main(int argc, const char* argv[]) {
    if (!InitSample(argc, argv)) {
        return 1;
    }
    init();

    while (!ShouldQuit()) {
        frame();
        utils::USleep(16000);
    }

    // TODO release stuff
}
