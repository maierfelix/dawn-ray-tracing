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
        //Device* device = ToBackend(GetDevice());

        return {};
    }

    RayTracingShaderBindingTable::~RayTracingShaderBindingTable() {
        //Device* device = ToBackend(GetDevice());

    }

}}  // namespace dawn_native::vulkan
