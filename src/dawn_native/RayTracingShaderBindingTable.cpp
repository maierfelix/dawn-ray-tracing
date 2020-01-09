// Copyright 2017 The Dawn Authors
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

#include "dawn_native/RayTracingShaderBindingTable.h"

#include "common/Assert.h"
#include "common/Math.h"
#include "dawn_native/Device.h"

namespace dawn_native {

    namespace {

        class ErrorRayTracingShaderBindingTable : public RayTracingShaderBindingTableBase {
          public:
            ErrorRayTracingShaderBindingTable(DeviceBase* device)
                : RayTracingShaderBindingTableBase(device, ObjectBase::kError) {
            }

          private:
            uint32_t GetOffsetImpl(wgpu::ShaderStage shaderStage) override {
                UNREACHABLE();
                return 0;
            }

            void DestroyImpl() override {
                UNREACHABLE();
            }
        };

    }  // anonymous namespace

    // RayTracingShaderBindingTable

    MaybeError ValidateRayTracingShaderBindingTableDescriptor(DeviceBase* device, const RayTracingShaderBindingTableDescriptor* descriptor) {
        if (descriptor->shaderCount == 0) {
            return DAWN_VALIDATION_ERROR("No shaders provided");
        }

        return {};
    }

    RayTracingShaderBindingTableBase::RayTracingShaderBindingTableBase(DeviceBase* device, const RayTracingShaderBindingTableDescriptor* descriptor)
        : ObjectBase(device) {

    }

    RayTracingShaderBindingTableBase::RayTracingShaderBindingTableBase(DeviceBase* device, ObjectBase::ErrorTag tag)
        : ObjectBase(device, tag) {
    }

    RayTracingShaderBindingTableBase::~RayTracingShaderBindingTableBase() {

    }

    uint32_t RayTracingShaderBindingTableBase::GetOffset(wgpu::ShaderStage shaderStage) {
        return GetOffsetImpl(shaderStage);
    }

    uint32_t RayTracingShaderBindingTableBase::GetOffsetImpl(wgpu::ShaderStage shaderStage) {
        return GetOffsetImpl(shaderStage);
    }

    void RayTracingShaderBindingTableBase::Destroy() {
        DestroyInternal();
    }

    void RayTracingShaderBindingTableBase::DestroyInternal() {
        if (!IsDestroyed()) {
            DestroyImpl();
        }
        SetDestroyState(true);
    }

    bool RayTracingShaderBindingTableBase::IsDestroyed() const {
        return mIsDestroyed;
    }

    void RayTracingShaderBindingTableBase::SetDestroyState(bool state) {
        mIsDestroyed = state;
    }

    // static
    RayTracingShaderBindingTableBase* RayTracingShaderBindingTableBase::MakeError(
        DeviceBase* device) {
        return new ErrorRayTracingShaderBindingTable(device);
    }

}  // namespace dawn_native
