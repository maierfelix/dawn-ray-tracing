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

#ifndef DAWNNATIVE_VULKAN_RAY_TRACING_ACCELERATION_CONTAINER_H_
#define DAWNNATIVE_VULKAN_RAY_TRACING_ACCELERATION_CONTAINER_H_

#include "common/vulkan_platform.h"
#include "dawn_native/RayTracingAccelerationContainer.h"
#include "dawn_native/ResourceMemoryAllocation.h"

#include <vector>

namespace dawn_native { namespace vulkan {

    class Device;

    struct VkAccelerationInstance {
        float transform[12];
        uint32_t instanceCustomIndex : 24;
        uint32_t mask : 8;
        uint32_t instanceOffset : 24;
        uint32_t flags : 8;
        uint64_t accelerationStructureHandle;
    };

    class RayTracingAccelerationContainer : public RayTracingAccelerationContainerBase {
      public:
        static ResultOrError<RayTracingAccelerationContainer*> Create(Device* device, const RayTracingAccelerationContainerDescriptor* descriptor);
        ~RayTracingAccelerationContainer();

        uint64_t GetHandle() const;
        VkAccelerationStructureTypeNV GetLevel() const;
        VkAccelerationStructureNV GetAccelerationStructure() const;
        VkMemoryRequirements2 RayTracingAccelerationContainer::GetMemoryRequirements(
            VkAccelerationStructureMemoryRequirementsTypeNV type) const;
        uint32_t RayTracingAccelerationContainer::GetMemoryRequirementSize(
            VkAccelerationStructureMemoryRequirementsTypeNV type) const;

      private:
        using RayTracingAccelerationContainerBase::RayTracingAccelerationContainerBase;

        std::vector<VkGeometryNV> mGeometries;
        std::vector<VkAccelerationInstance> mInstances;

        uint64_t mHandle = 0;
        VkAccelerationStructureTypeNV mLevel;
        VkAccelerationStructureNV mAccelerationStructure = VK_NULL_HANDLE;

        // scratch result memory
        uint32_t mScratchBufferResultOffset = 0x0;
        ResourceMemoryAllocation mScratchResultMemoryAllocation;
        // scratch build memory
        uint32_t mScratchBufferBuildOffset = 0x0;
        ResourceMemoryAllocation mScratchBuildMemoryAllocation;
        // scratch update memory
        uint32_t mScratchBufferUpdateOffset = 0x0;
        ResourceMemoryAllocation mScratchUpdateMemoryAllocation;

        MaybeError Initialize(const RayTracingAccelerationContainerDescriptor* descriptor);
    };

}}  // namespace dawn_native::vulkan

#endif  // DAWNNATIVE_VULKAN_RAY_TRACING_ACCELERATION_CONTAINER_H_
