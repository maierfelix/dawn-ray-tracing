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

#ifndef DAWNNATIVE_VULKAN_RAY_TRACING_ACCELERATION_INSTANCE_H_
#define DAWNNATIVE_VULKAN_RAY_TRACING_ACCELERATION_INSTANCE_H_

#include "common/vulkan_platform.h"
#include "dawn_native/RayTracingAccelerationInstance.h"
#include "dawn_native/vulkan/RayTracingAccelerationContainerVk.h"

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

    class RayTracingAccelerationInstance : public RayTracingAccelerationInstanceBase {
      public:
        static ResultOrError<RayTracingAccelerationInstance*> Create(Device* device, const RayTracingAccelerationInstanceDescriptor* descriptor);
        ~RayTracingAccelerationInstance();

        VkAccelerationInstance GetData() const;
        RayTracingAccelerationContainer* GetGeometryContainer() const;

        uint64_t GetHandle() const;
        void SetHandle(uint64_t handle);

      private:
        using RayTracingAccelerationInstanceBase::RayTracingAccelerationInstanceBase;
        MaybeError Initialize(const RayTracingAccelerationInstanceDescriptor* descriptor);

        VkAccelerationInstance mInstanceData;
        RayTracingAccelerationContainer* mGeometryContainer;
    };

}}  // namespace dawn_native::vulkan

#endif  // DAWNNATIVE_VULKAN_RAY_TRACING_ACCELERATION_INSTANCE_H_
