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

#ifndef DAWNNATIVE_D3D12_RAY_TRACING_PIPELINE_H_
#define DAWNNATIVE_D3D12_RAY_TRACING_PIPELINE_H_

#include <d3dcompiler.h>
#include <dxcapi.h>

#include <vector>

#include "dawn_native/RayTracingPipeline.h"
#include "dawn_native/d3d12/d3d12_platform.h"

namespace dawn_native { namespace d3d12 {

    class Device;

    class RayTracingPipeline : public RayTracingPipelineBase {
      public:
        static ResultOrError<RayTracingPipeline*> Create(
            Device* device,
            const RayTracingPipelineDescriptor* descriptor);
        ~RayTracingPipeline() override;

        void* GetShaderIdentifier(uint32_t shaderIndex);

        ComPtr<ID3D12StateObject> GetPipelineState();
        ComPtr<ID3D12StateObjectProperties> GetPipelineInfo();

      private:
        using RayTracingPipelineBase::RayTracingPipelineBase;
        MaybeError Initialize(const RayTracingPipelineDescriptor* descriptor);

        ComPtr<ID3D12StateObject> mPipelineState;
        ComPtr<ID3D12StateObjectProperties> mPipelineInfo;

        const wchar_t* mMainShaderEntry = L"main";

        std::vector<void*> mShaderExportIdentifiers;

        MaybeError CompileHLSLRayTracingShader(std::string& hlslSource, IDxcBlob** pShaderBlob);
    };

}}  // namespace dawn_native::d3d12

#endif  // DAWNNATIVE_D3D12_RAY_TRACING_PIPELINE_H_
