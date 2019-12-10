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

#include "dawn_native/vulkan/RayTracingAccelerationInstanceVk.h"

#include "dawn_native/vulkan/DeviceVk.h"
#include "dawn_native/vulkan/VulkanError.h"

namespace dawn_native { namespace vulkan {

    namespace {

    }  // anonymous namespace

    // static
    ResultOrError<RayTracingAccelerationInstance*> RayTracingAccelerationInstance::Create(Device* device, const RayTracingAccelerationInstanceDescriptor* descriptor) {
        std::unique_ptr<RayTracingAccelerationInstance> geometry = std::make_unique<RayTracingAccelerationInstance>(device, descriptor);
        DAWN_TRY(geometry->Initialize(descriptor));
        return geometry.release();
    }

    MaybeError RayTracingAccelerationInstance::Initialize(const RayTracingAccelerationInstanceDescriptor* descriptor) {
        if (descriptor->transform == nullptr) {
            return DAWN_VALIDATION_ERROR("Transform must be a valid Float32Array");
        }
        memcpy(
            &mInstanceData.transform,
            const_cast<float*>(descriptor->transform),
            sizeof(mInstanceData.transform)
        );

        mInstanceData.instanceCustomIndex = static_cast<uint32_t>(descriptor->instanceId);
        mInstanceData.mask = static_cast<uint32_t>(descriptor->mask);
        mInstanceData.instanceOffset = static_cast<uint32_t>(descriptor->instanceOffset);
        mInstanceData.flags = static_cast<uint32_t>(descriptor->flags);

        return {};
    }

    RayTracingAccelerationInstance::~RayTracingAccelerationInstance() {

    }

    VkAccelerationInstance RayTracingAccelerationInstance::GetData() const {
        return mInstanceData;
    }

    uint64_t RayTracingAccelerationInstance::GetHandle() const {
        return mInstanceData.accelerationStructureHandle;
    }
    void RayTracingAccelerationInstance::SetHandle(uint64_t handle) {
        mInstanceData.accelerationStructureHandle = handle;
    }

}}  // namespace dawn_native::vulkan
