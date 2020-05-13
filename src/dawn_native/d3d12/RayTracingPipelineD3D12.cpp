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

        bool IsValidDXBC(void* buffer) {
            if (buffer == NULL)
                return false;
            uint32_t offset = 0x0;
            uint32_t* dataU32 = (uint32_t*)buffer;
            uint32_t DXBC_MAGIC = (('D' << 0) + ('X' << 8) + ('B' << 16) + ('C' << 24));
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

        if (device->IsExtensionEnabled(Extension::RayTracing)) {
            if (!device->GetFunctions()->IsDXCAvailable()) {
                return DAWN_VALIDATION_ERROR(
                    "Ray tracing extension enabled, but DXC/DXIL unavailable");
            }
        }

        std::vector<RayTracingShaderBindingTableStageDescriptor>& stages = sbt->GetStages();
        std::vector<RayTracingShaderBindingTableGroupDescriptor>& groups = sbt->GetGroups();

        const wchar_t* mainShaderEntry = L"main";

        // Generate unique wchar ids for all stages
        std::vector<std::wstring> uniqueShaderStageIds(stages.size());
        std::vector<const wchar_t*> uniqueShaderStageIdPointers(stages.size());
        for (unsigned int ii = 0; ii < stages.size(); ++ii) {
            std::string id = std::to_string(ii);
            std::wstring name = L"S" + std::wstring(id.begin(), id.end());
            uniqueShaderStageIds[ii] = name;
            uniqueShaderStageIdPointers[ii] = uniqueShaderStageIds[ii].c_str();
        }

        // Generate unique wchar ids for all groups
        std::vector<std::wstring> uniqueShaderGroupIds(groups.size());
        std::vector<const wchar_t*> uniqueShaderGroupIdPointers(groups.size());
        for (unsigned int ii = 0; ii < groups.size(); ++ii) {
            std::string id = std::to_string(ii);
            std::wstring name = L"G" + std::wstring(id.begin(), id.end());
            uniqueShaderGroupIds[ii] = name;
            uniqueShaderGroupIdPointers[ii] = uniqueShaderGroupIds[ii].c_str();
        }

        // Find hitgroup count
        uint32_t hitGroupCount = 0;
        for (auto group : groups) {
            if (group.anyHitIndex != -1 || group.closestHitIndex != -1 ||
                group.intersectionIndex != -1) {
                hitGroupCount++;
            }
        }

        uint32_t subObjectIndex = 0;
        // clang-format off
        uint32_t subObjectCount = (
            stages.size() +    // stages
            hitGroupCount +    // hitgroups
            1 +                // shader config
            1 +                // shader config association
            1 +                // global root signature
            1                  // pipeline config
        );
        // clang-format on
        std::vector<D3D12_STATE_SUBOBJECT> subObjects(subObjectCount);

        // Lifetime objects
        std::vector<ComPtr<IDxcBlob>> shaderBlobs(stages.size());
        std::vector<D3D12_EXPORT_DESC> shaderExportDescs(stages.size());
        std::vector<D3D12_DXIL_LIBRARY_DESC> dxilLibraryDescs(stages.size());
        // Write shaders into subobjects
        for (unsigned int ii = 0; ii < stages.size(); ++ii) {
            RayTracingShaderBindingTableStageDescriptor& stage = stages[ii];
            // Generate HLSL
            std::string shaderSource;
            DAWN_TRY_ASSIGN(shaderSource, ToBackend(stage.module)->GetHLSLSource(layout));
            // Compile to DXBC
            DAWN_TRY(CompileHLSLRayTracingShader(shaderSource, &shaderBlobs[ii]));
            // Validate DXBC
            if (!IsValidDXBC(shaderBlobs[ii]->GetBufferPointer())) {
                return DAWN_VALIDATION_ERROR("DXBC is corrupted or unsigned");
            }
            // Shader export
            shaderExportDescs[ii].Name = uniqueShaderStageIdPointers[ii];
            shaderExportDescs[ii].ExportToRename = mainShaderEntry;
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

        // Lifetime objects
        std::vector<D3D12_HIT_GROUP_DESC> shaderHitGroups(groups.size());
        // Write hitgroups
        for (unsigned int ii = 0; ii < groups.size(); ++ii) {
            RayTracingShaderBindingTableGroupDescriptor& group = groups[ii];
            if (group.anyHitIndex != -1 || group.closestHitIndex != -1 ||
                group.intersectionIndex != -1) {
                shaderHitGroups[ii].Type = ToD3D12ShaderBindingTableGroupType(group.type);
                if (group.anyHitIndex != -1) {
                    shaderHitGroups[ii].AnyHitShaderImport =
                        uniqueShaderStageIdPointers[group.anyHitIndex];
                }
                if (group.closestHitIndex != -1) {
                    shaderHitGroups[ii].ClosestHitShaderImport =
                        uniqueShaderStageIdPointers[group.closestHitIndex];
                }
                if (group.intersectionIndex != -1) {
                    shaderHitGroups[ii].IntersectionShaderImport =
                        uniqueShaderStageIdPointers[group.intersectionIndex];
                }
                shaderHitGroups[ii].HitGroupExport = uniqueShaderGroupIdPointers[ii];
                // Write hitgroup object
                D3D12_STATE_SUBOBJECT hitObject;
                hitObject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
                hitObject.pDesc = &shaderHitGroups[ii];
                subObjects[subObjectIndex++] = hitObject;
            }
        }

        // Create shader config
        D3D12_RAYTRACING_SHADER_CONFIG shaderConfig;
        shaderConfig.MaxPayloadSizeInBytes = descriptor->rayTracingState->maxPayloadSize;
        shaderConfig.MaxAttributeSizeInBytes =
            D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;  // TODO: dynamic?
        D3D12_STATE_SUBOBJECT shaderConfigObject;
        shaderConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        shaderConfigObject.pDesc = &shaderConfig;
        subObjects[subObjectIndex++] = shaderConfigObject;

        // Associate shaders with the shader config
        D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION payloadAssocDesc;
        payloadAssocDesc.NumExports = uniqueShaderStageIdPointers.size();
        payloadAssocDesc.pExports = uniqueShaderStageIdPointers.data();
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

        // Pre-generate shader identifiers
        for (unsigned int ii = 0; ii < groups.size(); ++ii) {
            RayTracingShaderBindingTableGroupDescriptor& group = groups[ii];
            std::wstring prefix = group.generalIndex != -1 ? L"S" : L"G";
            std::string id = std::to_string(ii);
            std::wstring wideId = prefix + std::wstring(id.begin(), id.end());
            void* shaderIdentifier = mPipelineInfo.Get()->GetShaderIdentifier(wideId.c_str());
            if (shaderIdentifier == nullptr) {
                return DAWN_VALIDATION_ERROR("Failed to fetch SBT shader identifier");
            }
            mShaderExportIdentifiers.push_back(shaderIdentifier);
        }

        DAWN_TRY(sbt->Generate(this, layout));

        return {};
    }

    RayTracingPipeline::~RayTracingPipeline() {
        ToBackend(GetDevice())->ReferenceUntilUnused(mPipelineState);
    }

    MaybeError RayTracingPipeline::CompileHLSLRayTracingShader(std::string& hlslSource,
                                                               IDxcBlob** pShaderBlob) {
        Device* device = ToBackend(GetDevice());

        ComPtr<IDxcCompiler> pCompiler;
        ComPtr<IDxcLibrary> pLibrary;
        ComPtr<IDxcBlobEncoding> pHlslBlob;
        ComPtr<IDxcOperationResult> pHlslResult;

        DAWN_TRY(CheckHRESULT(
            device->GetFunctions()->dxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pCompiler)),
            "DXC create compiler"));

        DAWN_TRY(CheckHRESULT(
            device->GetFunctions()->dxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&pLibrary)),
            "DXC create library"));

        DAWN_TRY(CheckHRESULT(pLibrary->CreateBlobWithEncodingFromPinned(
                                  (unsigned char*)hlslSource.c_str(), (uint32_t)hlslSource.size(),
                                  0, pHlslBlob.GetAddressOf()),
                              "Create HLSL Blob"));

        DAWN_TRY(CheckHRESULT(pCompiler->Compile(pHlslBlob.Get(), L"", L"", L"lib_6_3", nullptr, 0,
                                                 nullptr, 0, nullptr, &pHlslResult),
                              "Compile HLSL Blob"));

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

    void* RayTracingPipeline::GetShaderIdentifier(uint32_t index) {
        return mShaderExportIdentifiers.at(index);
    }

    ID3D12StateObject* RayTracingPipeline::GetPipelineState() {
        return mPipelineState.Get();
    }

    ID3D12StateObjectProperties* RayTracingPipeline::GetPipelineInfo() {
        return mPipelineInfo.Get();
    }

}}  // namespace dawn_native::d3d12
