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

namespace dawn_native { namespace vulkan {

    class Device;

    class RayTracingAccelerationContainer : public RayTracingAccelerationContainerBase {
      public:
        static ResultOrError<RayTracingAccelerationContainer*> Create(Device* device, const RayTracingAccelerationContainerDescriptor* descriptor);
        ~RayTracingAccelerationContainer();

        uint64_t GetHandle() const;
        VkAccelerationStructureNV GetAccelerationStructure() const;

        int RayTracingAccelerationContainer::GetMemoryRequirementSize(
            VkAccelerationStructureMemoryRequirementsTypeNV type) const;

      private:
        using RayTracingAccelerationContainerBase::RayTracingAccelerationContainerBase;

        uint64_t mHandle = 0;
        VkAccelerationStructureNV mAccelerationStructure = VK_NULL_HANDLE;

        MaybeError Initialize(const RayTracingAccelerationContainerDescriptor* descriptor);
    };

}}  // namespace dawn_native::vulkan

#endif  // DAWNNATIVE_VULKAN_RAY_TRACING_ACCELERATION_CONTAINER_H_
