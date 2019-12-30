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

#ifndef DAWNNATIVE_RAY_TRACING_PIPELINE_H_
#define DAWNNATIVE_RAY_TRACING_PIPELINE_H_

#include "dawn_native/Pipeline.h"
#include "dawn_native/RayTracingShaderBindingTable.h"

namespace dawn_native {

    class DeviceBase;

    MaybeError ValidateRayTracingPipelineDescriptor(DeviceBase* device,
                                                    const RayTracingPipelineDescriptor* descriptor);

    class RayTracingPipelineBase : public PipelineBase {
      public:
        RayTracingPipelineBase(DeviceBase* device, const RayTracingPipelineDescriptor* descriptor);
        ~RayTracingPipelineBase() override;

        static RayTracingPipelineBase* MakeError(DeviceBase* device);

        RayTracingShaderBindingTableBase* GetShaderBindingTable();

      private:
        RayTracingPipelineBase(DeviceBase* device, ObjectBase::ErrorTag tag);

        Ref<RayTracingShaderBindingTableBase> mShaderBindingTable;
    };

}  // namespace dawn_native

#endif  // DAWNNATIVE_RAY_TRACING_PIPELINE_H_
