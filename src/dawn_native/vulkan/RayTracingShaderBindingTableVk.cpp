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

#include "dawn_native/vulkan/RayTracingShaderBindingTableVk.h"
#include "dawn_native/vulkan/FencedDeleter.h"
#include "dawn_native/vulkan/ResourceHeapVk.h"
#include "dawn_native/vulkan/ShaderModuleVk.h"

#include "dawn_native/vulkan/AdapterVk.h"
#include "dawn_native/vulkan/DeviceVk.h"
#include "dawn_native/vulkan/UtilsVulkan.h"
#include "dawn_native/vulkan/VulkanError.h"

namespace dawn_native { namespace vulkan {

    namespace {

    }  // anonymous namespace

    // static
    ResultOrError<RayTracingShaderBindingTable*> RayTracingShaderBindingTable::Create(
        Device* device,
        const RayTracingShaderBindingTableDescriptor* descriptor) {
        std::unique_ptr<RayTracingShaderBindingTable> sbt =
            std::make_unique<RayTracingShaderBindingTable>(device, descriptor);
        DAWN_TRY(sbt->Initialize(descriptor));
        return sbt.release();
    }

    void RayTracingShaderBindingTable::DestroyImpl() {
        if (mGroupMemory.buffer != VK_NULL_HANDLE) {
            Buffer* buffer = mGroupMemory.allocation.Get();
            if (buffer != nullptr) {
                buffer->Destroy();
            }
            mGroupMemory.buffer = VK_NULL_HANDLE;
        }
    }

    MaybeError RayTracingShaderBindingTable::Initialize(
        const RayTracingShaderBindingTableDescriptor* descriptor) {
        Device* device = ToBackend(GetDevice());
        Adapter* adapter = ToBackend(device->GetAdapter());

        // validate ray tracing calls
        if (device->fn.GetRayTracingShaderGroupHandlesNV == nullptr) {
            return DAWN_VALIDATION_ERROR("Invalid Call to GetRayTracingShaderGroupHandlesNV");
        }

        mRayTracingProperties = GetRayTracingProperties(*adapter);

        mStages.reserve(descriptor->stagesCount);
        for (unsigned int ii = 0; ii < descriptor->stagesCount; ++ii) {
            RayTracingShaderBindingTableStagesDescriptor stage = descriptor->stages[ii];
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.pNext = nullptr;
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = static_cast<VkShaderStageFlagBits>(ToVulkanShaderStageFlags(stage.stage));
            stageInfo.module = ToBackend(stage.module)->GetHandle();
            stageInfo.pName = "main";
            mStages.push_back(stageInfo);
        };

        mGroups.reserve(descriptor->groupsCount);
        for (unsigned int ii = 0; ii < descriptor->groupsCount; ++ii) {
            RayTracingShaderBindingTableGroupsDescriptor group = descriptor->groups[ii];
            VkRayTracingShaderGroupCreateInfoNV groupInfo{};
            groupInfo.pNext = nullptr;
            groupInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV;
            groupInfo.type = ToVulkanShaderBindingTableGroupType(group.type);
            groupInfo.generalShader = VK_SHADER_UNUSED_NV;
            groupInfo.closestHitShader = VK_SHADER_UNUSED_NV;
            groupInfo.anyHitShader = VK_SHADER_UNUSED_NV;
            groupInfo.intersectionShader = VK_SHADER_UNUSED_NV;
            if (group.generalIndex != -1) {
                if (!IsValidGroupStageIndex(group.generalIndex, VK_SHADER_STAGE_RAYGEN_BIT_NV) &&
                    !IsValidGroupStageIndex(group.generalIndex, VK_SHADER_STAGE_MISS_BIT_NV)) {
                    return DAWN_VALIDATION_ERROR("Invalid General group index");
                }
                groupInfo.generalShader = group.generalIndex;
            }
            if (group.closestHitIndex != -1) {
                if (!IsValidGroupStageIndex(group.closestHitIndex, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV)) {
                    return DAWN_VALIDATION_ERROR("Invalid Closest-Hit group index");
                }
                groupInfo.closestHitShader = group.closestHitIndex;
            }
            if (group.anyHitIndex != -1) {
                if (!IsValidGroupStageIndex(group.anyHitIndex, VK_SHADER_STAGE_ANY_HIT_BIT_NV)) {
                    return DAWN_VALIDATION_ERROR("Invalid Any-Hit group index");
                }
                groupInfo.anyHitShader = group.anyHitIndex;
            }
            if (group.intersectionIndex != -1) {
                if (!IsValidGroupStageIndex(group.intersectionIndex, VK_SHADER_STAGE_INTERSECTION_BIT_NV)) {
                    return DAWN_VALIDATION_ERROR("Invalid Intersection group index");
                }
                groupInfo.intersectionShader = group.intersectionIndex;
            }
            mGroups.push_back(groupInfo);
        };

        uint64_t bufferSize = mGroups.size() * GetShaderGroupHandleSize();

        BufferDescriptor bufferDescriptor = {nullptr, nullptr, wgpu::BufferUsage::MapWrite,
                                        bufferSize};
        Buffer* buffer = ToBackend(device->CreateBuffer(&bufferDescriptor));
        mGroupMemory.allocation = AcquireRef(buffer);
        mGroupMemory.buffer = buffer->GetHandle();
        mGroupMemory.offset = buffer->GetMemoryResource().GetOffset();
        mGroupMemory.memory =
            ToBackend(buffer->GetMemoryResource().GetResourceHeap())->GetMemory();
        mGroupMemory.resource = buffer->GetMemoryResource();

        return {};
    }

    RayTracingShaderBindingTable::~RayTracingShaderBindingTable() {
        DestroyInternal();
    }

    std::vector<VkRayTracingShaderGroupCreateInfoNV>& RayTracingShaderBindingTable::GetGroups() {
        return mGroups;
    }

    std::vector<VkPipelineShaderStageCreateInfo>& RayTracingShaderBindingTable::GetStages() {
        return mStages;
    }

    VkBuffer RayTracingShaderBindingTable::GetGroupBufferHandle() const {
        return mGroupMemory.buffer;
    }

    ResourceMemoryAllocation RayTracingShaderBindingTable::GetGroupBufferResource() const {
        return mGroupMemory.resource;
    }

    uint32_t RayTracingShaderBindingTable::GetShaderGroupHandleSize() const {
        return mRayTracingProperties.shaderGroupHandleSize;
    }

    bool RayTracingShaderBindingTable::IsValidGroupStageIndex(
        int32_t index,
        VkShaderStageFlagBits validStage) const {
        if (index < 0 || index >= (int32_t)mStages.size()) {
            return false;
        }
        VkShaderStageFlagBits stage = mStages[index].stage;
        if (stage != validStage) {
            std::string msg = "Invalid stage for group index '" + std::to_string(index) + "'";
            return false;
        }
        return true;
    }

}}  // namespace dawn_native::vulkan
