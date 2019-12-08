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

#include "dawn_native/RayTracingAccelerationGeometry.h"

#include "common/Assert.h"
#include "common/Math.h"
#include "dawn_native/Device.h"

namespace dawn_native {

    // RayTracingAccelerationGeometry

    MaybeError ValidateRayTracingAccelerationGeometryDescriptor(DeviceBase* device, const RayTracingAccelerationGeometryDescriptor* descriptor) {
        if (descriptor->vertexBuffer == nullptr) {
            return DAWN_VALIDATION_ERROR("vertexBuffer must be set");
        }
        if (descriptor->vertexStride <= 0) {
            return DAWN_VALIDATION_ERROR("vertexStride must be greater than zero");
        }
        return {};
    }

    RayTracingAccelerationGeometryBase::RayTracingAccelerationGeometryBase(DeviceBase* device, const RayTracingAccelerationGeometryDescriptor* descriptor)
        : ObjectBase(device) {

    }

    RayTracingAccelerationGeometryBase::RayTracingAccelerationGeometryBase(DeviceBase* device, ObjectBase::ErrorTag tag)
        : ObjectBase(device, tag) {
    }

    // static
    RayTracingAccelerationGeometryBase* RayTracingAccelerationGeometryBase::MakeError(DeviceBase* device) {
        return new RayTracingAccelerationGeometryBase(device, ObjectBase::kError);
    }

}  // namespace dawn_native
