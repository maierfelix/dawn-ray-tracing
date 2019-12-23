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
#include "dawn_native/vulkan/ResourceHeapVk.h"
#include "dawn_native/vulkan/StagingBufferVk.h"

#include "dawn_native/vulkan/DeviceVk.h"
#include "dawn_native/vulkan/VulkanError.h"
#include "dawn_native/vulkan/AdapterVk.h"

namespace dawn_native { namespace vulkan {

    namespace {

    }  // anonymous namespace

    // static
    ResultOrError<RayTracingShaderBindingTable*> RayTracingShaderBindingTable::Create(
        Device* device,
        const RayTracingShaderBindingTableDescriptor* descriptor) {
        std::unique_ptr<RayTracingShaderBindingTable> geometry =
            std::make_unique<RayTracingShaderBindingTable>(device, descriptor);
        DAWN_TRY(geometry->Initialize(descriptor));
        return geometry.release();
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

        if (descriptor->shaderCount > 0) {
            for (unsigned int ii = 0; ii < descriptor->shaderCount; ++ii) {
                RayTracingShaderBindingTableShadersDescriptor shader = descriptor->shaders[ii];
                auto type = VK_SHADER_UNUSED_NV;
                auto generalShader = VK_SHADER_UNUSED_NV;
                auto closestHitShader = VK_SHADER_UNUSED_NV;
                auto anyHitShader = VK_SHADER_UNUSED_NV;
                auto intersectionShader = VK_SHADER_UNUSED_NV;
                switch (shader.stage) {
                    case wgpu::ShaderStage::RayGeneration:
                        type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
                        generalShader = mStages.size();
                        break;
                    case wgpu::ShaderStage::RayClosestHit:
                        type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV;
                        closestHitShader = mStages.size();
                        break;
                    case wgpu::ShaderStage::RayMiss:
                        type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
                        generalShader = mStages.size();
                        break;
                };
                VkRayTracingShaderGroupCreateInfoNV stageInfo = {};
                stageInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV;
                stageInfo.type = (VkRayTracingShaderGroupTypeNV) type;
                stageInfo.generalShader = generalShader;
                stageInfo.closestHitShader = closestHitShader;
                stageInfo.anyHitShader = anyHitShader;
                stageInfo.intersectionShader = intersectionShader;
                mStages.push_back(stageInfo);
            };
        }

        return {};
    }

    RayTracingShaderBindingTable::~RayTracingShaderBindingTable() {

    }

    std::vector<VkRayTracingShaderGroupCreateInfoNV>& RayTracingShaderBindingTable::GetStages() {
        return mStages;
    }

    uint32_t RayTracingShaderBindingTable::GetShaderGroupHandleSize() const {
        return mRayTracingProperties.shaderGroupHandleSize;
    }

    uint32_t RayTracingShaderBindingTable::GetOffsetImpl(wgpu::ShaderStage stageKind) {
        return 1337;
    }

}}  // namespace dawn_native::vulkan
