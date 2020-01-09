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

#ifndef DAWNNATIVE_RAY_TRACING_ACCELERATION_CONTAINER_H_
#define DAWNNATIVE_RAY_TRACING_ACCELERATION_CONTAINER_H_

#include "dawn_native/Error.h"
#include "dawn_native/Forward.h"
#include "dawn_native/ObjectBase.h"

#include "dawn_native/dawn_platform.h"

#include <vector>
#include <memory>

namespace dawn_native {

    MaybeError ValidateRayTracingAccelerationContainerDescriptor(
        DeviceBase* device,
        const RayTracingAccelerationContainerDescriptor* descriptor);

    class RayTracingAccelerationContainerBase : public ObjectBase {

      public:
        RayTracingAccelerationContainerBase(
            DeviceBase* device,
            const RayTracingAccelerationContainerDescriptor* descriptor);

        static RayTracingAccelerationContainerBase* MakeError(DeviceBase* device);

        void Destroy();

        bool IsBuilt() const;
        bool IsUpdated() const;
        bool IsDestroyed() const;
        void RayTracingAccelerationContainerBase::SetBuildState(bool state);
        void RayTracingAccelerationContainerBase::SetUpdateState(bool state);
        void RayTracingAccelerationContainerBase::SetDestroyState(bool state);

        MaybeError ValidateCanUseInSubmitNow() const;

        wgpu::RayTracingAccelerationContainerFlag GetFlags() const;
        wgpu::RayTracingAccelerationContainerLevel GetLevel() const;

      protected:
        RayTracingAccelerationContainerBase(DeviceBase* device, ObjectBase::ErrorTag tag);

        void DestroyInternal();

      private:

        // bottom-level references
        std::vector<Ref<BufferBase>> mVertexBuffers;
        std::vector<Ref<BufferBase>> mIndexBuffers;

        // top-level references
        std::vector<Ref<RayTracingAccelerationContainerBase>> mGeometryContainers;

        bool mIsBuilt = false;
        bool mIsUpdated = false;
        bool mIsDestroyed = false;

        wgpu::RayTracingAccelerationContainerFlag mFlags;
        wgpu::RayTracingAccelerationContainerLevel mLevel;

        virtual void DestroyImpl() = 0;
    };

}  // namespace dawn_native

#endif  // DAWNNATIVE_RAY_TRACING_ACCELERATION_CONTAINER_H_