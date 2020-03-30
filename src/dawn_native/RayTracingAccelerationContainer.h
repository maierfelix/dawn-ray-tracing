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

#include <memory>
#include <vector>

#include "dawn_native/Error.h"
#include "dawn_native/Forward.h"
#include "dawn_native/ObjectBase.h"
#include "dawn_native/dawn_platform.h"

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
        uint64_t GetHandle();
        void UpdateInstance(uint32_t instanceIndex,
                            const RayTracingAccelerationInstanceDescriptor* descriptor);

        bool IsBuilt() const;
        bool IsUpdated() const;
        bool IsDestroyed() const;
        void SetBuildState(bool state);
        void SetUpdateState(bool state);
        void SetDestroyState(bool state);

        MaybeError ValidateCanUseInSubmitNow() const;

        wgpu::RayTracingAccelerationContainerFlag GetFlags() const;
        wgpu::RayTracingAccelerationContainerLevel GetLevel() const;

      protected:
        RayTracingAccelerationContainerBase(DeviceBase* device, ObjectBase::ErrorTag tag);

        void DestroyInternal();
        uint64_t GetHandleInternal();

      private:
        // bottom-level references
        std::vector<Ref<BufferBase>> mVertexBuffers;
        std::vector<Ref<BufferBase>> mIndexBuffers;
        std::vector<Ref<BufferBase>> mAABBBuffers;

        // top-level references
        Ref<BufferBase> mInstanceBuffer;
        std::vector<Ref<RayTracingAccelerationContainerBase>> mGeometryContainers;

        bool mIsBuilt = false;
        bool mIsUpdated = false;
        bool mIsDestroyed = false;

        wgpu::RayTracingAccelerationContainerFlag mFlags;
        wgpu::RayTracingAccelerationContainerLevel mLevel;

        MaybeError ValidateUpdateInstance(
            uint32_t instanceIndex,
            const RayTracingAccelerationInstanceDescriptor* descriptor) const;

        virtual void DestroyImpl() = 0;
        virtual uint64_t GetHandleImpl() = 0;
        virtual MaybeError UpdateInstanceImpl(
            uint32_t instanceIndex,
            const RayTracingAccelerationInstanceDescriptor* descriptor) = 0;
    };

}  // namespace dawn_native

#endif  // DAWNNATIVE_RAY_TRACING_ACCELERATION_CONTAINER_H_