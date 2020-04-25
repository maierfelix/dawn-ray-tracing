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

#include <vector>

#include "common/vulkan_platform.h"
#include "dawn_native/RayTracingAccelerationContainer.h"
#include "dawn_native/vulkan/BufferVk.h"

namespace dawn_native { namespace vulkan {

    class Device;

    struct ScratchMemoryPool {
        MemoryEntry result;
        MemoryEntry update;
        MemoryEntry build;
    };

    class RayTracingAccelerationContainer : public RayTracingAccelerationContainerBase {
      public:
        static ResultOrError<RayTracingAccelerationContainer*> Create(
            Device* device,
            const RayTracingAccelerationContainerDescriptor* descriptor);
        ~RayTracingAccelerationContainer() override;

        uint64_t GetHandle() const;
        VkAccelerationStructureKHR GetAccelerationStructure() const;

        std::vector<VkAccelerationStructureGeometryKHR>& GetGeometries();
        std::vector<VkAccelerationStructureBuildOffsetInfoKHR>& GetBuildOffsets();

        MemoryEntry& GetInstanceMemory();

        ScratchMemoryPool& GetScratchMemory();
        void DestroyScratchBuildMemory();

      private:
        using RayTracingAccelerationContainerBase::RayTracingAccelerationContainerBase;

        void DestroyImpl() override;
        MaybeError UpdateInstanceImpl(
            uint32_t instanceIndex,
            const RayTracingAccelerationInstanceDescriptor* descriptor) override;

        std::vector<VkAccelerationStructureGeometryKHR> mGeometries;
        std::vector<VkAccelerationStructureBuildOffsetInfoKHR> mBuildOffsets;

        // AS related
        uint64_t mAccelerationHandle;
        VkAccelerationStructureKHR mAccelerationStructure = VK_NULL_HANDLE;

        // scratch memory
        ScratchMemoryPool mScratchMemory;

        // instance buffer
        MemoryEntry mInstanceMemory;

        VkMemoryRequirements GetMemoryRequirements(
            VkAccelerationStructureMemoryRequirementsTypeKHR type) const;
        uint64_t GetMemoryRequirementSize(
            VkAccelerationStructureMemoryRequirementsTypeKHR type) const;

        MaybeError CreateAccelerationStructure(
            const RayTracingAccelerationContainerDescriptor* descriptor);

        MaybeError AllocateScratchMemory(MemoryEntry& memoryEntry,
                                         VkMemoryRequirements& requirements);

        MaybeError Initialize(const RayTracingAccelerationContainerDescriptor* descriptor);
    };

}}  // namespace dawn_native::vulkan

#endif  // DAWNNATIVE_VULKAN_RAY_TRACING_ACCELERATION_CONTAINER_H_