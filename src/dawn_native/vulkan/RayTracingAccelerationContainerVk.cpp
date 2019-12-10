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

#include "dawn_native/vulkan/RayTracingAccelerationContainerVk.h"

#include "dawn_native/vulkan/DeviceVk.h"
#include "dawn_native/vulkan/VulkanError.h"

namespace dawn_native { namespace vulkan {

    namespace {

        VkAccelerationStructureTypeNV VulkanAccelerationContainerLevel(wgpu::RayTracingAccelerationContainerLevel containerLevel) {
            VkAccelerationStructureTypeNV level = static_cast<VkAccelerationStructureTypeNV>(0);
            if (containerLevel == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
                level = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
            }
            if (containerLevel == wgpu::RayTracingAccelerationContainerLevel::Top) {
                level = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
            }
            return level;
        }

        VkBuildAccelerationStructureFlagBitsNV VulkanBuildAccelerationStructureFlags(wgpu::RayTracingAccelerationContainerFlag buildFlags) {
            uint32_t flags = 0;
            if (buildFlags & wgpu::RayTracingAccelerationContainerFlag::AllowUpdate) {
                flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV;
            }
            if (buildFlags & wgpu::RayTracingAccelerationContainerFlag::PreferFastBuild) {
                flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_NV;
            }
            if (buildFlags & wgpu::RayTracingAccelerationContainerFlag::PreferFastTrace) {
                flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV;
            }
            if (buildFlags & wgpu::RayTracingAccelerationContainerFlag::LowMemory) {
                flags |= VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_NV;
            }
            return static_cast<VkBuildAccelerationStructureFlagBitsNV>(flags);
        }

    }  // anonymous namespace

    // static
    ResultOrError<RayTracingAccelerationContainer*> RayTracingAccelerationContainer::Create(Device* device, const RayTracingAccelerationContainerDescriptor* descriptor) {
        std::unique_ptr<RayTracingAccelerationContainer> geometry = std::make_unique<RayTracingAccelerationContainer>(device, descriptor);
        DAWN_TRY(geometry->Initialize(descriptor));
        return geometry.release();
    }

    MaybeError RayTracingAccelerationContainer::Initialize(const RayTracingAccelerationContainerDescriptor* descriptor) {
        //VkBuildAccelerationStructureFlagBitsNV flags = VulkanBuildAccelerationStructureFlags(descriptor->flags);
        //VkAccelerationStructureTypeNV type = VulkanAccelerationContainerLevel(descriptor->level);
        return {};
    }

    RayTracingAccelerationContainer::~RayTracingAccelerationContainer() {

    }

}}  // namespace dawn_native::vulkan
