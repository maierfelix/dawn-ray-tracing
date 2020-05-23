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

#ifndef DAWNNATIVE_RAY_TRACING_PASSENCODER_H_
#define DAWNNATIVE_RAY_TRACING_PASSENCODER_H_

#include "dawn_native/Error.h"
#include "dawn_native/ProgrammablePassEncoder.h"

namespace dawn_native {

    class RayTracingPassEncoder final : public ProgrammablePassEncoder {
      public:
        RayTracingPassEncoder(DeviceBase* device,
                              CommandEncoder* commandEncoder,
                              EncodingContext* encodingContext);

        static RayTracingPassEncoder* MakeError(DeviceBase* device,
                                                CommandEncoder* commandEncoder,
                                                EncodingContext* encodingContext);

        void EndPass();

        void TraceRays(uint32_t rayGenerationOffset,
                       uint32_t rayHitOffset,
                       uint32_t rayMissOffset,
                       uint32_t width,
                       uint32_t height,
                       uint32_t depth);
        void SetPipeline(RayTracingPipelineBase* pipeline);

      protected:
        RayTracingPassEncoder(DeviceBase* device,
                              CommandEncoder* commandEncoder,
                              EncodingContext* encodingContext,
                              ErrorTag errorTag);

      private:
        // For render and compute passes, the encoding context is borrowed from the command encoder.
        // Keep a reference to the encoder to make sure the context isn't freed.
        Ref<CommandEncoder> mCommandEncoder;
    };

}  // namespace dawn_native

#endif  // DAWNNATIVE_RAY_TRACING_PASSENCODER_H_
