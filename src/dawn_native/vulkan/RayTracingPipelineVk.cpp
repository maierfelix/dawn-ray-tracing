// Copyright 2018 The Dawn Authors
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

#include "dawn_native/vulkan/RayTracingPipelineVk.h"

#include "dawn_native/vulkan/DeviceVk.h"
#include "dawn_native/vulkan/FencedDeleter.h"
#include "dawn_native/vulkan/PipelineLayoutVk.h"
#include "dawn_native/vulkan/ShaderModuleVk.h"
#include "dawn_native/vulkan/RayTracingShaderBindingTableVk.h"
#include "dawn_native/vulkan/VulkanError.h"
#include "dawn_native/vulkan/UtilsVulkan.h"
#include "dawn_native/vulkan/ResourceHeapVk.h"

namespace dawn_native { namespace vulkan {

    // static
    ResultOrError<RayTracingPipeline*> RayTracingPipeline::Create(
        Device* device,
        const RayTracingPipelineDescriptor* descriptor) {
        std::unique_ptr<RayTracingPipeline> pipeline =
            std::make_unique<RayTracingPipeline>(device, descriptor);
        DAWN_TRY(pipeline->Initialize(descriptor));
        return pipeline.release();
    }

    MaybeError RayTracingPipeline::Initialize(const RayTracingPipelineDescriptor* descriptor) {
        Device* device = ToBackend(GetDevice());

        RayTracingShaderBindingTable* shaderBindingTable =
            ToBackend(descriptor->rayTracingState->shaderBindingTable);

        std::vector<VkPipelineShaderStageCreateInfo> stages = shaderBindingTable->GetStages();
        std::vector<VkRayTracingShaderGroupCreateInfoNV> groups = shaderBindingTable->GetGroups();

        {
            VkRayTracingPipelineCreateInfoNV createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV;
            createInfo.pNext = nullptr;
            createInfo.flags = 0;
            createInfo.pStages = stages.data();
            createInfo.stageCount = stages.size();
            createInfo.pGroups = groups.data();
            createInfo.groupCount = groups.size();
            createInfo.maxRecursionDepth = descriptor->rayTracingState->maxRecursionDepth;
            createInfo.layout = ToBackend(descriptor->layout)->GetHandle();
            createInfo.basePipelineHandle = VK_NULL_HANDLE;
            createInfo.basePipelineIndex = -1;

            MaybeError result = CheckVkSuccess(
                device->fn.CreateRayTracingPipelinesNV(device->GetVkDevice(), VK_NULL_HANDLE, 1,
                                                       &createInfo, nullptr, &mHandle),
                "vkCreateRayTracingPipelinesNV");
            if (result.IsError())
                return result.AcquireError();
        }

        {
            uint64_t bufferSize = stages.size() * shaderBindingTable->GetShaderGroupHandleSize();

            VkBufferCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            createInfo.pNext = nullptr;
            createInfo.flags = 0;
            createInfo.size = bufferSize;
            createInfo.usage =
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;
            createInfo.pQueueFamilyIndices = 0;

            Device* device = ToBackend(GetDevice());
            DAWN_TRY(CheckVkSuccess(
                device->fn.CreateBuffer(device->GetVkDevice(), &createInfo, nullptr, &mGroupBuffer),
                "vkCreateBuffer"));

            VkMemoryRequirements requirements;
            device->fn.GetBufferMemoryRequirements(device->GetVkDevice(), mGroupBuffer,
                                                   &requirements);

            DAWN_TRY_ASSIGN(mGroupBufferResource, device->AllocateMemory(requirements, true));

            DAWN_TRY(
                CheckVkSuccess(device->fn.BindBufferMemory(
                                   device->GetVkDevice(), mGroupBuffer,
                                   ToBackend(mGroupBufferResource.GetResourceHeap())->GetMemory(),
                                   mGroupBufferResource.GetOffset()),
                               "vkBindBufferMemory"));
            MaybeError result =
                CheckVkSuccess(device->fn.GetRayTracingShaderGroupHandlesNV(
                                   device->GetVkDevice(), mHandle, 0, stages.size(), bufferSize,
                                   mGroupBufferResource.GetMappedPointer()),
                               "GetRayTracingShaderGroupHandlesNV");
            if (result.IsError())
                return result.AcquireError();

        }

        return {};
    }

    RayTracingPipeline::~RayTracingPipeline() {
        if (mHandle != VK_NULL_HANDLE) {
            ToBackend(GetDevice())->GetFencedDeleter()->DeleteWhenUnused(mHandle);
            mHandle = VK_NULL_HANDLE;
        }
    }

    VkPipeline RayTracingPipeline::GetHandle() const {
        return mHandle;
    }

    VkBuffer RayTracingPipeline::GetGroupBufferHandle() const {
        return mGroupBuffer;
    }

}}  // namespace dawn_native::vulkan
