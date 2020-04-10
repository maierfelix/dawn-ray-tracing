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

#include "dawn_native/d3d12/RayTracingPipelineD3D12.h"

#include <d3dcompiler.h>

#include "common/Assert.h"
#include "dawn_native/d3d12/D3D12Error.h"
#include "dawn_native/d3d12/DeviceD3D12.h"
#include "dawn_native/d3d12/PipelineLayoutD3D12.h"
#include "dawn_native/d3d12/PlatformFunctions.h"
#include "dawn_native/d3d12/RayTracingShaderBindingTableD3D12.h"
#include "dawn_native/d3d12/ShaderModuleD3D12.h"
#include "dawn_native/d3d12/TextureD3D12.h"
#include "dawn_native/d3d12/UtilsD3D12.h"

namespace dawn_native { namespace d3d12 {

    // static
    ResultOrError<RayTracingPipeline*> RayTracingPipeline::Create(
        Device* device,
        const RayTracingPipelineDescriptor* descriptor) {
        std::unique_ptr<RayTracingPipeline> pipeline =
            std::make_unique<RayTracingPipeline>(device, descriptor);
        DAWN_TRY(pipeline->Initialize(descriptor));
        return pipeline.release();
    }

    MaybeError RayTracingPipeline::Initialize(const RayTracingPipelineDescriptor* descriptor) {
        Device* device = ToBackend(GetDevice());

        RayTracingShaderBindingTable* shaderBindingTable =
            ToBackend(descriptor->rayTracingState->shaderBindingTable);

        std::vector<ShaderModule*> stages = shaderBindingTable->GetStages();

        uint32_t compileFlags = 0;
        compileFlags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
        for (unsigned int ii = 0; ii < stages.size(); ++ii) {
            ShaderModule* module = stages[ii];
            std::string hlslSource;
            DAWN_TRY_ASSIGN(hlslSource, module->GetHLSLSource(ToBackend(GetLayout())));

            ComPtr<ID3DBlob> compiledShader;
            ComPtr<ID3DBlob> errors;

            const PlatformFunctions* functions = device->GetFunctions();
            if (FAILED(functions->d3dCompile(hlslSource.c_str(), hlslSource.length(), nullptr,
                                             nullptr, nullptr, "main", "cs_5_1", compileFlags, 0,
                                             &compiledShader, &errors))) {
                printf("%s\n", reinterpret_cast<char*>(errors->GetBufferPointer()));
                ASSERT(false);
            }
        };

        return {};
    }

    RayTracingPipeline::~RayTracingPipeline() {
        ToBackend(GetDevice())->ReferenceUntilUnused(mPipelineState);
    }

    ComPtr<ID3D12PipelineState> RayTracingPipeline::GetPipelineState() {
        return mPipelineState;
    }

}}  // namespace dawn_native::d3d12
