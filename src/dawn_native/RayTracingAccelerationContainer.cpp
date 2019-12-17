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

#include "dawn_native/RayTracingAccelerationContainer.h"

#include "common/Assert.h"
#include "common/Math.h"
#include "dawn_native/Device.h"

namespace dawn_native {

    // RayTracingAccelerationContainer

    MaybeError ValidateRayTracingAccelerationContainerDescriptor(DeviceBase* device, const RayTracingAccelerationContainerDescriptor* descriptor) {
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Top) {
            if (descriptor->geometryCount > 0) {
                return DAWN_VALIDATION_ERROR("Geometry Count for Top-Level Acceleration Container must be zero");
            }
        }
        if (descriptor->level == wgpu::RayTracingAccelerationContainerLevel::Bottom) {
            if (descriptor->instanceCount > 0) {
                return DAWN_VALIDATION_ERROR("Instance Count for Bottom-Level Acceleration Container must be zero");
            }
        }
        if (descriptor->geometryCount == 0 && descriptor->instanceCount == 0) {
            return DAWN_VALIDATION_ERROR(
                "No data provided for Acceleration Container");
        }
        return {};
    }

    RayTracingAccelerationContainerBase::RayTracingAccelerationContainerBase(DeviceBase* device, const RayTracingAccelerationContainerDescriptor* descriptor)
        : ObjectBase(device) {

    }

    RayTracingAccelerationContainerBase::RayTracingAccelerationContainerBase(DeviceBase* device, ObjectBase::ErrorTag tag)
        : ObjectBase(device, tag) {
    }

    // static
    RayTracingAccelerationContainerBase* RayTracingAccelerationContainerBase::MakeError(DeviceBase* device) {
        return new RayTracingAccelerationContainerBase(device, ObjectBase::kError);
    }

    bool RayTracingAccelerationContainerBase::IsBuilt() const {
        return mIsBuilt;
    }

}  // namespace dawn_native