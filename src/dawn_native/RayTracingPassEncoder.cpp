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

#include "dawn_native/RayTracingPassEncoder.h"

#include "dawn_native/Buffer.h"
#include "dawn_native/CommandEncoder.h"
#include "dawn_native/Commands.h"
#include "dawn_native/RayTracingPipeline.h"
#include "dawn_native/Device.h"

namespace dawn_native {

    RayTracingPassEncoder::RayTracingPassEncoder(DeviceBase* device,
                                           CommandEncoder* commandEncoder,
                                           EncodingContext* encodingContext)
        : ProgrammablePassEncoder(device, encodingContext), mCommandEncoder(commandEncoder) {
    }

    RayTracingPassEncoder::RayTracingPassEncoder(DeviceBase* device,
                                           CommandEncoder* commandEncoder,
                                           EncodingContext* encodingContext,
                                           ErrorTag errorTag)
        : ProgrammablePassEncoder(device, encodingContext, errorTag),
          mCommandEncoder(commandEncoder) {
    }

    RayTracingPassEncoder* RayTracingPassEncoder::MakeError(DeviceBase* device,
                                                      CommandEncoder* commandEncoder,
                                                      EncodingContext* encodingContext) {
        return new RayTracingPassEncoder(device, commandEncoder, encodingContext,
                                         ObjectBase::kError);
    }

    void RayTracingPassEncoder::EndPass() {
        if (mEncodingContext->TryEncode(this, [&](CommandAllocator* allocator) -> MaybeError {
                allocator->Allocate<EndRayTracingPassCmd>(Command::EndRayTracingPass);

                return {};
            })) {
            mEncodingContext->ExitPass(this, mUsageTracker.AcquireResourceUsage());
        }
    }

    void RayTracingPassEncoder::TraceRays(uint32_t rayGenerationOffset,
                                          uint32_t rayHitOffset,
                                          uint32_t rayMissOffset,
                                          uint32_t width,
                                          uint32_t height,
                                          uint32_t depth) {
        mEncodingContext->TryEncode(this, [&](CommandAllocator* allocator) -> MaybeError {
            TraceRaysCmd* traceRays = allocator->Allocate<TraceRaysCmd>(Command::TraceRays);
            traceRays->rayGenerationOffset = rayGenerationOffset;
            traceRays->rayHitOffset = rayHitOffset;
            traceRays->rayMissOffset = rayMissOffset;
            traceRays->width = width;
            traceRays->height = height;
            traceRays->depth = depth;
            return {};
        });
    }

    void RayTracingPassEncoder::SetPipeline(RayTracingPipelineBase* pipeline) {
        mEncodingContext->TryEncode(this, [&](CommandAllocator* allocator) -> MaybeError {
            DAWN_TRY(GetDevice()->ValidateObject(pipeline));

            if (pipeline->GetShaderBindingTable()->IsDestroyed()) {
                return DAWN_VALIDATION_ERROR("Shader binding table is destroyed");
            }

            SetRayTracingPipelineCmd* setPipeline =
                allocator->Allocate<SetRayTracingPipelineCmd>(Command::SetRayTracingPipeline);
            setPipeline->pipeline = pipeline;

            return {};
        });
    }

}  // namespace dawn_native
