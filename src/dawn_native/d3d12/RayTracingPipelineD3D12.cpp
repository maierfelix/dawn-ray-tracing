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

        ComPtr<IDxcBlob> pRayGenByteCode;
        ComPtr<IDxcBlob> pRayClosestHitByteCode;
        ComPtr<IDxcBlob> pRayMissByteCode;

        RayTracingShaderBindingTable* sbt =
            ToBackend(descriptor->rayTracingState->shaderBindingTable);

        std::vector<RayTracingShaderBindingTableStagesDescriptor>& stages = sbt->GetStages();

        // TODO: Make dynamic

        // Compile shaders
        std::string rgenSource;
        DAWN_TRY_ASSIGN(rgenSource, ToBackend(stages[0].module)->GetHLSLSource(layout));
        DAWN_TRY(CompileHLSLRayTracingShader(rgenSource, &pRayGenByteCode));
        std::string rchitSource;
        DAWN_TRY_ASSIGN(rchitSource, ToBackend(stages[1].module)->GetHLSLSource(layout));
        DAWN_TRY(CompileHLSLRayTracingShader(rchitSource, &pRayClosestHitByteCode));
        std::string rmissSource;
        DAWN_TRY_ASSIGN(rmissSource, ToBackend(stages[2].module)->GetHLSLSource(layout));
        DAWN_TRY(CompileHLSLRayTracingShader(rmissSource, &pRayMissByteCode));

        // Validate each shader
        if (!IsSignedDXIL(pRayGenByteCode->GetBufferPointer())) {
            return DAWN_VALIDATION_ERROR("DXIL is unsigned or invalid");
        }
        if (!IsSignedDXIL(pRayClosestHitByteCode->GetBufferPointer())) {
            return DAWN_VALIDATION_ERROR("DXIL is unsigned or invalid");
        }
        if (!IsSignedDXIL(pRayMissByteCode->GetBufferPointer())) {
            return DAWN_VALIDATION_ERROR("DXIL is unsigned or invalid");
        }

        std::vector<D3D12_STATE_SUBOBJECT> subObjects(8);

        // Ray Generation Object
        D3D12_EXPORT_DESC rayGenExportDesc;
        rayGenExportDesc.Name = L"rgen_main";
        rayGenExportDesc.ExportToRename = L"main";
        rayGenExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;
        D3D12_DXIL_LIBRARY_DESC rayGenDesc;
        rayGenDesc.DXILLibrary.BytecodeLength = pRayGenByteCode->GetBufferSize();
        rayGenDesc.DXILLibrary.pShaderBytecode = pRayGenByteCode->GetBufferPointer();
        rayGenDesc.NumExports = 1;
        rayGenDesc.pExports = &rayGenExportDesc;
        D3D12_STATE_SUBOBJECT rayGenObject;
        rayGenObject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        rayGenObject.pDesc = &rayGenDesc;
        subObjects[0] = rayGenObject;

        // Ray Closest-Hit Object
        D3D12_EXPORT_DESC rayClosestHitExportDesc;
        rayClosestHitExportDesc.Name = L"rchit_main";
        rayClosestHitExportDesc.ExportToRename = L"main";
        rayClosestHitExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;
        D3D12_DXIL_LIBRARY_DESC rayClosestHitDesc;
        rayClosestHitDesc.DXILLibrary.BytecodeLength = pRayClosestHitByteCode->GetBufferSize();
        rayClosestHitDesc.DXILLibrary.pShaderBytecode = pRayClosestHitByteCode->GetBufferPointer();
        rayClosestHitDesc.NumExports = 1;
        rayClosestHitDesc.pExports = &rayClosestHitExportDesc;
        D3D12_STATE_SUBOBJECT rayClosestHitObject;
        rayClosestHitObject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        rayClosestHitObject.pDesc = &rayClosestHitDesc;
        subObjects[1] = rayClosestHitObject;

        // Ray Miss Object
        D3D12_EXPORT_DESC rayMissExportDesc;
        rayMissExportDesc.Name = L"rmiss_main";
        rayMissExportDesc.ExportToRename = L"main";
        rayMissExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;
        D3D12_DXIL_LIBRARY_DESC rayMissDesc;
        rayMissDesc.DXILLibrary.BytecodeLength = pRayMissByteCode->GetBufferSize();
        rayMissDesc.DXILLibrary.pShaderBytecode = pRayMissByteCode->GetBufferPointer();
        rayMissDesc.NumExports = 1;
        rayMissDesc.pExports = &rayMissExportDesc;
        D3D12_STATE_SUBOBJECT rayMissObject;
        rayMissObject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        rayMissObject.pDesc = &rayMissDesc;
        subObjects[2] = rayMissObject;

        D3D12_HIT_GROUP_DESC hitDesc;
        hitDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        hitDesc.AnyHitShaderImport = nullptr;
        hitDesc.ClosestHitShaderImport = L"rchit_main";
        hitDesc.IntersectionShaderImport = nullptr;
        hitDesc.HitGroupExport = L"HitGroup_0";
        D3D12_STATE_SUBOBJECT hitSubObject;
        hitSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
        hitSubObject.pDesc = &hitDesc;
        subObjects[3] = hitSubObject;

        // Create shader configuration
        D3D12_RAYTRACING_SHADER_CONFIG shaderConfig;
        shaderConfig.MaxPayloadSizeInBytes = 4 * sizeof(float);  // TODO: dynamic
        shaderConfig.MaxAttributeSizeInBytes =
            D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;  // TODO: dynamic
        D3D12_STATE_SUBOBJECT shaderConfigObject;
        shaderConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        shaderConfigObject.pDesc = &shaderConfig;
        subObjects[4] = shaderConfigObject;

        // Associate shaders with the payload config
        const WCHAR* shaderPayloadExports[] = {L"rgen_main", L"HitGroup_0", L"rmiss_main"};
        D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION payloadAssocDesc;
        payloadAssocDesc.NumExports = _countof(shaderPayloadExports);
        payloadAssocDesc.pExports = shaderPayloadExports;
        payloadAssocDesc.pSubobjectToAssociate = &subObjects[4];
        D3D12_STATE_SUBOBJECT payloadAssocObject;
        payloadAssocObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
        payloadAssocObject.pDesc = &payloadAssocDesc;
        subObjects[5] = payloadAssocObject;

        // create global root signature
        ID3D12RootSignature* pInterface = layout->GetRootSignature().Get();
        D3D12_STATE_SUBOBJECT globalRootSignatureObject;
        globalRootSignatureObject.pDesc = &pInterface;
        globalRootSignatureObject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
        subObjects[6] = globalRootSignatureObject;

        // Create Pipeline config
        D3D12_RAYTRACING_PIPELINE_CONFIG rtPipelineConfig;
        rtPipelineConfig.MaxTraceRecursionDepth = 1;
        D3D12_STATE_SUBOBJECT rtPipelineConfigObject;
        rtPipelineConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
        rtPipelineConfigObject.pDesc = &rtPipelineConfig;
        subObjects[7] = rtPipelineConfigObject;

        // Create Pipeline
        D3D12_STATE_OBJECT_DESC stateObjectDesc;
        stateObjectDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        stateObjectDesc.NumSubobjects = subObjects.size();
        stateObjectDesc.pSubobjects = subObjects.data();

        DAWN_TRY(CheckHRESULT(device->GetD3D12Device5()->CreateStateObject(
                                  &stateObjectDesc, IID_PPV_ARGS(&mPipelineState)),
                              "Create RT pipeline"));

        sbt->Generate(this, layout);

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

    ComPtr<ID3D12StateObject> RayTracingPipeline::GetPipelineState() {
        return mPipelineState;
    }

}}  // namespace dawn_native::d3d12
