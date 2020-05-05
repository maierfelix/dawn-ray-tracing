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

    namespace {

        std::string ConvertBlobToString(IDxcBlobEncoding* pBlob) {
            std::vector<char> infoLog(pBlob->GetBufferSize() + 1);
            memcpy(infoLog.data(), pBlob->GetBufferPointer(), pBlob->GetBufferSize());
            infoLog[pBlob->GetBufferSize()] = 0;
            return std::string(infoLog.data());
        }

    }  // namespace

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
        PipelineLayout* layout = ToBackend(descriptor->layout);

        RayTracingShaderBindingTable* sbt =
            ToBackend(descriptor->rayTracingState->shaderBindingTable);

        std::vector<RayTracingShaderBindingTableStagesDescriptor>& stages = sbt->GetStages();

        std::vector<RayTracingShaderBindingTableStagesDescriptor> hitGroups;
        for (unsigned int ii = 0; ii < stages.size(); ++ii) {
            if (stages[ii].stage == wgpu::ShaderStage::RayClosestHit ||
                stages[ii].stage == wgpu::ShaderStage::RayAnyHit) {
                hitGroups.push_back(stages[ii]);
            }
        };

        for (unsigned int ii = 0; ii < stages.size(); ++ii) {
            ShaderModule* module = ToBackend(stages[ii].module);

            std::string hlslSource;
            DAWN_TRY_ASSIGN(hlslSource, module->GetHLSLSource(layout));

            ComPtr<IDxcBlob> pShaderBlob;
            CompileHLSLRayTracingShader(hlslSource, &pShaderBlob);
            mDXILLibraries.push_back(pShaderBlob);
        }

        uint32_t m_rootSignatureAssociations = 0;
        // clang-format off
        uint64_t subObjectCount = (
            mDXILLibraries.size() +
            hitGroups.size() +
            1 +
            1 +
            2 * m_rootSignatureAssociations +
            2 +
            1
        );
        // clang-format on

        // fixed entry "main" for all shaders (for now)
        D3D12_EXPORT_DESC shaderExportDesc;
        shaderExportDesc.Name = L"main";
        shaderExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;
        shaderExportDesc.ExportToRename = nullptr;

        std::vector<D3D12_STATE_SUBOBJECT> subObjects(subObjectCount);

        uint32_t subObjectIndex = 0;

        // Add DXIL libs
        for (unsigned int ii = 0; ii < mDXILLibraries.size(); ++ii) {
            ComPtr<IDxcBlob> pShaderBlob = mDXILLibraries[ii];
            D3D12_DXIL_LIBRARY_DESC dxilLibDesc;
            dxilLibDesc.DXILLibrary.pShaderBytecode = pShaderBlob.Get()->GetBufferPointer();
            dxilLibDesc.DXILLibrary.BytecodeLength = pShaderBlob.Get()->GetBufferSize();
            dxilLibDesc.NumExports = 1;
            dxilLibDesc.pExports = &shaderExportDesc;

            D3D12_STATE_SUBOBJECT libStateObject;
            libStateObject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
            libStateObject.pDesc = &dxilLibDesc;
            subObjects[subObjectIndex++] = libStateObject;
        }

        // Add hit groups
        for (unsigned int ii = 0; ii < hitGroups.size(); ++ii) {
            bool isAnyHit = hitGroups[ii].stage == wgpu::ShaderStage::RayAnyHit;
            bool isClosestHit = hitGroups[ii].stage == wgpu::ShaderStage::RayClosestHit;
            D3D12_HIT_GROUP_DESC hitGroupDesc;
            hitGroupDesc.HitGroupExport = L"Main";
            hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
            hitGroupDesc.AnyHitShaderImport = isAnyHit ? L"main" : L"";
            hitGroupDesc.ClosestHitShaderImport = isClosestHit ? L"main" : L"";
            hitGroupDesc.IntersectionShaderImport = L"";

            D3D12_STATE_SUBOBJECT libStateObject;
            libStateObject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
            libStateObject.pDesc = &hitGroupDesc;
            subObjects[subObjectIndex++] = libStateObject;
        }

        // Add shader config
        {
            D3D12_RAYTRACING_SHADER_CONFIG rtShaderConfig;
            rtShaderConfig.MaxPayloadSizeInBytes = 4 * sizeof(float);    // TODO: dynamic
            rtShaderConfig.MaxAttributeSizeInBytes = 2 * sizeof(float);  // TODO: dynamic

            D3D12_STATE_SUBOBJECT rtShaderConfigObject;
            rtShaderConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
            rtShaderConfigObject.pDesc = &rtShaderConfig;

            subObjects[subObjectIndex++] = rtShaderConfigObject;
        }

        return {};
    }

    RayTracingPipeline::~RayTracingPipeline() {
        ToBackend(GetDevice())->ReferenceUntilUnused(mPipelineState);
    }

    MaybeError RayTracingPipeline::CompileHLSLRayTracingShader(std::string& hlslSource,
                                                               IDxcBlob** pShaderBlob) {
        Device* device = ToBackend(GetDevice());
        // TODO: can these be cached?
        ComPtr<IDxcCompiler> pCompiler;
        DAWN_TRY(CheckHRESULT(
            device->GetFunctions()->dxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pCompiler)),
            "DXC create compiler"));
        ComPtr<IDxcLibrary> pLibrary;
        DAWN_TRY(CheckHRESULT(
            device->GetFunctions()->dxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&pLibrary)),
            "DXC create library"));

        ComPtr<IDxcBlobEncoding> pHlslBlob;
        DAWN_TRY(CheckHRESULT(pLibrary->CreateBlobWithEncodingFromPinned(
                                  (LPBYTE)hlslSource.c_str(),
                                  static_cast<uint32_t>(hlslSource.size()), 0, &pHlslBlob),
                              "Create HLSL Blob"));

        ComPtr<IDxcOperationResult> pHlslResult;
        DAWN_TRY(CheckHRESULT(pCompiler->Compile(pHlslBlob.Get(), L"", L"", L"lib_6_3", nullptr, 0,
                                                 nullptr, 0, nullptr, &pHlslResult),
                              "Compile HLSL Blob"));

        // Verify the result
        HRESULT resultCode;
        DAWN_TRY(
            CheckHRESULT(pHlslResult->GetStatus(&resultCode), "Verify HLSL compilation status"));
        if (FAILED(resultCode)) {
            ComPtr<IDxcBlobEncoding> pHlslError;
            DAWN_TRY(CheckHRESULT(pHlslResult->GetErrorBuffer(&pHlslError),
                                  "Failed to retrieve compilation error"));
            std::string log = ConvertBlobToString(pHlslError.Get());
            return DAWN_VALIDATION_ERROR(log);
        }

        DAWN_TRY(CheckHRESULT(pHlslResult->GetResult(pShaderBlob), "HLSL shader blob"));

        return {};
    }

    ComPtr<ID3D12PipelineState> RayTracingPipeline::GetPipelineState() {
        return mPipelineState;
    }

}}  // namespace dawn_native::d3d12
