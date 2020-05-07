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

        bool IsSignedDXIL(LPVOID buffer) {
            uint32_t offset = 0x0;
            UINT32* dataU32 = (UINT32*)buffer;
            UINT32 DXBC_MAGIC = (('D' << 0) + ('X' << 8) + ('B' << 16) + ('C' << 24));
            bool out = false;
            // validate header
            out |= dataU32[offset++] != DXBC_MAGIC;
            // check for zero-filled appendencies
            out |= dataU32[offset++] != 0x0;
            out |= dataU32[offset++] != 0x0;
            out |= dataU32[offset++] != 0x0;
            out |= dataU32[offset++] != 0x0;
            return out;
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
        Device* device = ToBackend(GetDevice());

        PipelineLayout* layout = ToBackend(descriptor->layout);

        RayTracingShaderBindingTable* sbt =
            ToBackend(descriptor->rayTracingState->shaderBindingTable);

        std::vector<RayTracingShaderBindingTableStagesDescriptor>& stages = sbt->GetStages();

        // Find the largest ray payload of all SBT shaders
        uint32_t maxPayloadSize = 0;
        for (auto stage : stages) {
            ShaderModule* module = ToBackend(stage.module);
            maxPayloadSize = std::max(maxPayloadSize, module->GetMaxRayPayloadSize());
        }

        // Generate a unique wchar id for each shader
        std::vector<const wchar_t*> uniqueShaderIdentifiers;
        for (unsigned int ii = 0; ii < stages.size(); ++ii) {
            std::string id = std::to_string(ii);
            std::wstring wideId = std::wstring(id.begin(), id.end());
            // TODO: improve this
            wchar_t* heapId = new wchar_t[id.size()];
            wcscpy(heapId, wideId.c_str());
            uniqueShaderIdentifiers.push_back(heapId);
        }

        uint32_t subObjectIndex = 0;
        // clang-format off
        uint32_t subObjectCount = (
            stages.size() + // stages
            1 +             // hit group
            1 +             // shader config
            1 +             // shader config association
            1 +             // global root signature
            1               // pipeline config
        );
        // clang-format on
        std::vector<D3D12_STATE_SUBOBJECT> subObjects(subObjectCount);

        // Lifetime objects
        std::vector<ComPtr<IDxcBlob>> shaderBlobs(stages.size());
        std::vector<D3D12_EXPORT_DESC> shaderExportDescs(stages.size());
        std::vector<D3D12_DXIL_LIBRARY_DESC> dxilLibraryDescs(stages.size());
        // Write shaders into subobjects
        for (unsigned int ii = 0; ii < stages.size(); ++ii) {
            RayTracingShaderBindingTableStagesDescriptor& stage = stages[ii];
            // Generate HLSL
            std::string shaderSource;
            DAWN_TRY_ASSIGN(shaderSource, ToBackend(stage.module)->GetHLSLSource(layout));
            // Compile to DXBC
            DAWN_TRY(CompileHLSLRayTracingShader(shaderSource, &shaderBlobs[ii]));
            // Validate DXBC
            if (!IsSignedDXIL(shaderBlobs[ii]->GetBufferPointer())) {
                return DAWN_VALIDATION_ERROR("DXIL is unsigned or invalid");
            }
            // Shader export
            shaderExportDescs[ii].Name = uniqueShaderIdentifiers[ii];
            shaderExportDescs[ii].ExportToRename = mMainShaderEntry;
            shaderExportDescs[ii].Flags = D3D12_EXPORT_FLAG_NONE;
            // Shader library
            dxilLibraryDescs[ii].DXILLibrary.BytecodeLength = shaderBlobs[ii]->GetBufferSize();
            dxilLibraryDescs[ii].DXILLibrary.pShaderBytecode = shaderBlobs[ii]->GetBufferPointer();
            dxilLibraryDescs[ii].NumExports = 1;
            dxilLibraryDescs[ii].pExports = &shaderExportDescs[ii];
            // Write shader object
            D3D12_STATE_SUBOBJECT shaderObject;
            shaderObject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
            shaderObject.pDesc = &dxilLibraryDescs[ii];
            subObjects[subObjectIndex++] = shaderObject;
        }

        // Ray hit group
        D3D12_HIT_GROUP_DESC hitDesc;
        hitDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        hitDesc.AnyHitShaderImport = nullptr;
        hitDesc.ClosestHitShaderImport = uniqueShaderIdentifiers[1];
        hitDesc.IntersectionShaderImport = nullptr;
        hitDesc.HitGroupExport = L"HitGroup_0";
        D3D12_STATE_SUBOBJECT hitSubObject;
        hitSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
        hitSubObject.pDesc = &hitDesc;
        subObjects[subObjectIndex++] = hitSubObject;

        // Create shader config
        D3D12_RAYTRACING_SHADER_CONFIG shaderConfig;
        shaderConfig.MaxPayloadSizeInBytes = maxPayloadSize;
        shaderConfig.MaxAttributeSizeInBytes =
            D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;  // TODO: dynamic?
        D3D12_STATE_SUBOBJECT shaderConfigObject;
        shaderConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        shaderConfigObject.pDesc = &shaderConfig;
        subObjects[subObjectIndex++] = shaderConfigObject;

        // Associate shaders with the shader config
        D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION payloadAssocDesc;
        payloadAssocDesc.NumExports = uniqueShaderIdentifiers.size();
        payloadAssocDesc.pExports = uniqueShaderIdentifiers.data();
        payloadAssocDesc.pSubobjectToAssociate = &subObjects[subObjectIndex - 1];
        D3D12_STATE_SUBOBJECT payloadAssocObject;
        payloadAssocObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
        payloadAssocObject.pDesc = &payloadAssocDesc;
        subObjects[subObjectIndex++] = payloadAssocObject;

        // Create global root signature
        ID3D12RootSignature* pInterface = layout->GetRootSignature().Get();
        D3D12_STATE_SUBOBJECT globalRootSignatureObject;
        globalRootSignatureObject.pDesc = &pInterface;
        globalRootSignatureObject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
        subObjects[subObjectIndex++] = globalRootSignatureObject;

        // Create pipeline config
        D3D12_RAYTRACING_PIPELINE_CONFIG rtPipelineConfig;
        rtPipelineConfig.MaxTraceRecursionDepth = descriptor->rayTracingState->maxRecursionDepth;
        D3D12_STATE_SUBOBJECT rtPipelineConfigObject;
        rtPipelineConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
        rtPipelineConfigObject.pDesc = &rtPipelineConfig;
        subObjects[subObjectIndex++] = rtPipelineConfigObject;

        // Create pipeline
        D3D12_STATE_OBJECT_DESC stateObjectDesc;
        stateObjectDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        stateObjectDesc.NumSubobjects = subObjects.size();
        stateObjectDesc.pSubobjects = subObjects.data();
        DAWN_TRY(CheckHRESULT(device->GetD3D12Device5()->CreateStateObject(
                                  &stateObjectDesc, IID_PPV_ARGS(&mPipelineState)),
                              "Create RT pipeline"));

        // Gather pipeline info
        DAWN_TRY(CheckHRESULT(mPipelineState->QueryInterface(IID_PPV_ARGS(&mPipelineInfo)),
                              "Query RT pipeline info"));

        // Pre-generate shader identifier
        for (unsigned int ii = 0; ii < stages.size(); ++ii) {
            std::string id = std::to_string(ii);
            std::wstring wideId = std::wstring(id.begin(), id.end());
            void* shaderIdentifier = mPipelineInfo.Get()->GetShaderIdentifier(wideId.c_str());
            mShaderExportIdentifiers.push_back(shaderIdentifier);
        }

        sbt->Generate(this, layout);

        // free unique wchar ids
        for (auto id : uniqueShaderIdentifiers)
            delete id;

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
        DAWN_TRY(CheckHRESULT(
            pLibrary->CreateBlobWithEncodingFromPinned(
                (BYTE*)hlslSource.c_str(), (UINT32)hlslSource.size(), 0, pHlslBlob.GetAddressOf()),
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
            std::string error = ConvertBlobToString(pHlslError.Get());
            return DAWN_VALIDATION_ERROR(error);
        }

        DAWN_TRY(CheckHRESULT(pHlslResult->GetResult(pShaderBlob), "HLSL shader blob"));

        return {};
    }

    void* RayTracingPipeline::GetShaderIdentifier(uint32_t shaderIndex) {
        return mShaderExportIdentifiers.at(shaderIndex);
    }

    ComPtr<ID3D12StateObject> RayTracingPipeline::GetPipelineState() {
        return mPipelineState;
    }

    ComPtr<ID3D12StateObjectProperties> RayTracingPipeline::GetPipelineInfo() {
        return mPipelineInfo;
    }

}}  // namespace dawn_native::d3d12
