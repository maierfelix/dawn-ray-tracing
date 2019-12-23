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

#ifndef DAWNNATIVE_RAY_TRACING_SHADER_BINDING_TABLE_H_
#define DAWNNATIVE_RAY_TRACING_SHADER_BINDING_TABLE_H_

#include "dawn_native/Error.h"
#include "dawn_native/Forward.h"
#include "dawn_native/ObjectBase.h"

#include "dawn_native/dawn_platform.h"

#include <memory>

namespace dawn_native {

    MaybeError ValidateRayTracingShaderBindingTableDescriptor(DeviceBase* device,
                                           const RayTracingShaderBindingTableDescriptor* descriptor);

    class RayTracingShaderBindingTableBase : public ObjectBase {
      public:
        RayTracingShaderBindingTableBase(DeviceBase* device,
                                         const RayTracingShaderBindingTableDescriptor* descriptor);
        ~RayTracingShaderBindingTableBase();

        static RayTracingShaderBindingTableBase* MakeError(DeviceBase* device);

        uint32_t GetOffset(wgpu::ShaderStage stageKind);

      protected:
        RayTracingShaderBindingTableBase(DeviceBase* device, ObjectBase::ErrorTag tag);

      private:
        virtual uint32_t GetOffsetImpl(wgpu::ShaderStage stageKind);

    };

}  // namespace dawn_native

#endif  // DAWNNATIVE_RAY_TRACING_SHADER_BINDING_TABLE_H_