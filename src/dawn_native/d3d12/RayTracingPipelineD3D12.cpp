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

    static const char* hlslSource = R"(
        struct RayPayload
        {
            float3 color;
        };

        //RaytracingAccelerationStructure topLevelAS : register(t0, space0);
        RWTexture2D<float4> pixelBuffer : register(u0, space0);

        static RayPayload payload;
        [shader("raygeneration")] void rgen_main() {
        float2 pixelCenter = float2(DispatchRaysIndex().xy) + 0.5f.xx;
        float2 uv = pixelCenter / float2(DispatchRaysDimensions().xy);
        float2 d = (uv * 2.0f) - 1.0f.xx;
        float aspectRatio =
            float(DispatchRaysDimensions().x) / float(DispatchRaysDimensions().y);
        float3 direction = normalize(float3(d.x * aspectRatio, d.y, 1.0f));
        payload.color = 0.0f.xxx;
        {
            RayDesc ray;
            ray.Origin = float3(0.0f, 0.0f, -1.5f);
            ray.Direction = direction;
            ray.TMin = 0.001000000047497451305389404296875f;
            ray.TMax = 100.0f;
            //TraceRay(topLevelAS, 1u, 255u, 0u, 0u, 0u, ray, payload);
        }
        uint pixelIndex = ((DispatchRaysDimensions().y - DispatchRaysIndex().y) *
                            DispatchRaysDimensions().x) +
                            DispatchRaysIndex().x;
        pixelBuffer[int2(int3(DispatchRaysIndex()).xy)] = float4(payload.color, 1.0);
        }

        struct HitAttributeData
        {
            float2 bary;
        };

        [shader("closesthit")] void rchit_main(inout RayPayload payload, in HitAttributeData attribs)
        {
            float3 bary = float3((1.0f - attribs.bary.x) - attribs.bary.y, attribs.bary.x, attribs.bary.y);
            payload.color = bary;
        }

        [shader("miss")] void rmiss_main(inout RayPayload payload)
        {
            payload.color = 0.1500000059604644775390625f.xxx;
        }
    )";

    namespace {

        std::string ConvertBlobToString(IDxcBlobEncoding* pBlob) {
            std::vector<char> infoLog(pBlob->GetBufferSize() + 1);
            memcpy(infoLog.data(), pBlob->GetBufferPointer(), pBlob->GetBufferSize());
            infoLog[pBlob->GetBufferSize()] = 0;
            return std::string(infoLog.data());
        }
        bool isSignedDXIL(LPVOID buffer) {
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

        // Combined shader
        std::string hlslSourceStr = hlslSource;
        ComPtr<IDxcBlob> pHLSLByteCode;
        DAWN_TRY(CompileHLSLRayTracingShader(hlslSourceStr, &pHLSLByteCode));

        if (!isSignedDXIL(pHLSLByteCode->GetBufferPointer())) {
            return DAWN_VALIDATION_ERROR("DXIL is unsigned or invalid");
        }

        std::vector<D3D12_STATE_SUBOBJECT> subObjects(6);

        std::vector<std::wstring> entryPoints;
        entryPoints.push_back(L"rgen_main");
        entryPoints.push_back(L"rchit_main");
        entryPoints.push_back(L"rmiss_main");
        entryPoints.push_back(L"HitGroup_0");

        std::vector<D3D12_EXPORT_DESC> exportDesc;
        exportDesc.push_back({entryPoints[0].c_str(), nullptr, D3D12_EXPORT_FLAG_NONE});
        exportDesc.push_back({entryPoints[1].c_str(), nullptr, D3D12_EXPORT_FLAG_NONE});
        exportDesc.push_back({entryPoints[2].c_str(), nullptr, D3D12_EXPORT_FLAG_NONE});

        D3D12_DXIL_LIBRARY_DESC dxilLibDesc;
        D3D12_STATE_SUBOBJECT stateSubobject;
        stateSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        stateSubobject.pDesc = &dxilLibDesc;
        dxilLibDesc.DXILLibrary.pShaderBytecode = pHLSLByteCode->GetBufferPointer();
        dxilLibDesc.DXILLibrary.BytecodeLength = pHLSLByteCode->GetBufferSize();
        dxilLibDesc.NumExports = exportDesc.size();
        dxilLibDesc.pExports = exportDesc.data();
        subObjects[0] = stateSubobject;

        D3D12_HIT_GROUP_DESC hitDesc;
        hitDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        hitDesc.AnyHitShaderImport = nullptr;
        hitDesc.ClosestHitShaderImport = entryPoints[1].c_str();
        hitDesc.IntersectionShaderImport = nullptr;
        hitDesc.HitGroupExport = entryPoints[3].c_str();
        D3D12_STATE_SUBOBJECT hitSubObject;
        hitSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
        hitSubObject.pDesc = &hitDesc;
        subObjects[1] = hitSubObject;

        // Create shader configuration
        D3D12_RAYTRACING_SHADER_CONFIG shaderConfig;
        shaderConfig.MaxPayloadSizeInBytes = 4 * sizeof(float);  // TODO: dynamic
        shaderConfig.MaxAttributeSizeInBytes =
            D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;  // TODO: dynamic
        D3D12_STATE_SUBOBJECT shaderConfigObject;
        shaderConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        shaderConfigObject.pDesc = &shaderConfig;
        subObjects[2] = shaderConfigObject;

        // Associate shaders with the payload config
        // List of shader using the shaderConfig payload
        const WCHAR* shaderPayloadExports[] = {entryPoints[2].c_str(), entryPoints[1].c_str(),
                                               entryPoints[0].c_str()};
        D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION payloadAssocDesc;
        payloadAssocDesc.NumExports = _countof(shaderPayloadExports);
        payloadAssocDesc.pExports = shaderPayloadExports;
        payloadAssocDesc.pSubobjectToAssociate = &subObjects[2];
        D3D12_STATE_SUBOBJECT payloadAssocObject;
        payloadAssocObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
        payloadAssocObject.pDesc = &payloadAssocDesc;
        subObjects[3] = payloadAssocObject;

        // create global root signature
        ID3D12RootSignature* pInterface = layout->GetRootSignature().Get();
        D3D12_STATE_SUBOBJECT globalRootSignatureObject;
        globalRootSignatureObject.pDesc = &pInterface;
        globalRootSignatureObject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
        subObjects[4] = globalRootSignatureObject;

        // Create Pipeline config
        D3D12_RAYTRACING_PIPELINE_CONFIG rtPipelineConfig;
        rtPipelineConfig.MaxTraceRecursionDepth = 1;
        D3D12_STATE_SUBOBJECT rtPipelineConfigObject;
        rtPipelineConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
        rtPipelineConfigObject.pDesc = &rtPipelineConfig;
        subObjects[5] = rtPipelineConfigObject;

        // Create Pipeline
        D3D12_STATE_OBJECT_DESC stateObjectDesc;
        stateObjectDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        stateObjectDesc.NumSubobjects = subObjects.size();
        stateObjectDesc.pSubobjects = subObjects.data();

        DAWN_TRY(CheckHRESULT(device->GetD3D12Device5()->CreateStateObject(
                                  &stateObjectDesc, IID_PPV_ARGS(&mPipelineState)),
                              "Create RT pipeline"));

        DAWN_TRY(CheckHRESULT(mPipelineState->QueryInterface(IID_PPV_ARGS(&mPipelineInfo)),
                              "Query RT pipeline info"));

        return {};
    }

    /*
    {
        std::vector<D3D12_STATE_SUBOBJECT> subObjects;

        // Add libs
        for (unsigned int ii = 0; ii < mDXILLibraries.size(); ++ii) {
            ComPtr<IDxcBlob> pShaderBlob = mDXILLibraries[ii];
            D3D12_DXIL_LIBRARY_DESC dxilLibDesc;
            dxilLibDesc.NumExports = 1;
            dxilLibDesc.pExports = &shaderExportDesc;

            // Add shader blob
            D3D12_SHADER_BYTECODE dxilShaderByteCode;
            dxilShaderByteCode.pShaderBytecode = pShaderBlob.Get()->GetBufferPointer();
            dxilShaderByteCode.BytecodeLength = pShaderBlob.Get()->GetBufferSize();
            dxilLibDesc.DXILLibrary = dxilShaderByteCode;

            // Write subobject
            D3D12_STATE_SUBOBJECT dxilLibObject;
            dxilLibObject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
            dxilLibObject.pDesc = &dxilLibDesc;
            subObjects.push_back(dxilLibObject);
        }

        // Add hit groups
        for (unsigned int ii = 0; ii < hitGroups.size(); ++ii) {
            bool isAnyHit = hitGroups[ii].stage == wgpu::ShaderStage::RayAnyHit;
            bool isClosestHit = hitGroups[ii].stage == wgpu::ShaderStage::RayClosestHit;

            if (!isAnyHit && !isClosestHit)
                continue;
            D3D12_HIT_GROUP_DESC hitGroupDesc;
            hitGroupDesc.HitGroupExport = L"HitGroup";
            hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
            hitGroupDesc.AnyHitShaderImport = isAnyHit ? L"main" : L"";
            hitGroupDesc.ClosestHitShaderImport = isClosestHit ? L"main" : L"";
            hitGroupDesc.IntersectionShaderImport = L"";

            // Write subobject
            D3D12_STATE_SUBOBJECT hitGroupObject;
            hitGroupObject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
            hitGroupObject.pDesc = &hitGroupDesc;
            subObjects.push_back(hitGroupObject);
        }

        // Add global root signature
        {
            ComPtr<ID3D12RootSignature> rootSignature = layout->GetRootSignature();

            // Write subobject
            D3D12_STATE_SUBOBJECT globalRootSignatureObject;
            globalRootSignatureObject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
            globalRootSignatureObject.pDesc = rootSignature.Get();
            subObjects.push_back(globalRootSignatureObject);
        }

        // Add local root signature
        {
            // Write subobject
            D3D12_STATE_SUBOBJECT localRootSignatureObject;
            localRootSignatureObject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
            localRootSignatureObject.pDesc = nullptr;
            subObjects.push_back(localRootSignatureObject);
        }

        // Add shader config
        {
            D3D12_RAYTRACING_SHADER_CONFIG rtShaderConfig;
            rtShaderConfig.MaxPayloadSizeInBytes = 4 * sizeof(float);    // TODO: dynamic
            rtShaderConfig.MaxAttributeSizeInBytes = 2 * sizeof(float);  // TODO: dynamic

            D3D12_STATE_SUBOBJECT rtShaderConfigObject;
            rtShaderConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
            rtShaderConfigObject.pDesc = &rtShaderConfig;

            // Write subobject
            subObjects.push_back(rtShaderConfigObject);
        }

        // Add pipeline config
        {
            D3D12_RAYTRACING_PIPELINE_CONFIG rtPipelineConfig;
            rtPipelineConfig.MaxTraceRecursionDepth = 1;

            D3D12_STATE_SUBOBJECT rtPipelineConfigObject;
            rtPipelineConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
            rtPipelineConfigObject.pDesc = &rtPipelineConfig;

            // Write subobject
            subObjects.push_back(rtPipelineConfigObject);
        }

    }
    */

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
