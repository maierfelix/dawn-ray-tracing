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

    static const WCHAR* kRayGenShader = L"rgen_main";
    static const WCHAR* kMissShader = L"rmiss_main";
    static const WCHAR* kClosestHitShader = L"rchit_main";
    static const WCHAR* kHitGroup = L"HitGroup";

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

        const char* hlslSource = R"(
            struct RayPayload
            {
                float3 color;
            };
            /*
            RaytracingAccelerationStructure topLevelAS : register(t0, space0);
            RWTexture2D<float4> pixelBuffer : register(u0, space0);
            */
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
            //pixelBuffer[int2(int3(DispatchRaysIndex()).xy)] = float4(payload.color, 1.0);
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
        /*
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
        */
        // Combined shader
        std::string hlslSourceStr = hlslSource;
        ComPtr<IDxcBlob> pHLSLByteCode;
        DAWN_TRY(CompileHLSLRayTracingShader(hlslSourceStr, &pHLSLByteCode));

        if (!isSignedDXIL(pHLSLByteCode->GetBufferPointer())) {
            return DAWN_VALIDATION_ERROR("DXIL is not signed");
        }

        std::array<D3D12_STATE_SUBOBJECT, 5> subobjects;
        uint32_t index = 0;

        std::vector<std::wstring> entryPoints;
        entryPoints.resize(3);
        entryPoints[0] = kRayGenShader;
        entryPoints[1] = kClosestHitShader;
        entryPoints[2] = kMissShader;

        std::vector<D3D12_EXPORT_DESC> exportDesc;
        exportDesc.resize(3);
        exportDesc[0].Name = entryPoints[0].c_str();
        exportDesc[0].Flags = D3D12_EXPORT_FLAG_NONE;
        exportDesc[0].ExportToRename = nullptr;
        exportDesc[1].Name = entryPoints[1].c_str();
        exportDesc[1].Flags = D3D12_EXPORT_FLAG_NONE;
        exportDesc[1].ExportToRename = nullptr;
        exportDesc[2].Name = entryPoints[2].c_str();
        exportDesc[2].Flags = D3D12_EXPORT_FLAG_NONE;
        exportDesc[2].ExportToRename = nullptr;

        D3D12_DXIL_LIBRARY_DESC dxilLibDesc{};
        D3D12_STATE_SUBOBJECT stateSubobject{};
        stateSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        stateSubobject.pDesc = &dxilLibDesc;
        dxilLibDesc.DXILLibrary.pShaderBytecode = pHLSLByteCode->GetBufferPointer();
        dxilLibDesc.DXILLibrary.BytecodeLength = pHLSLByteCode->GetBufferSize();
        dxilLibDesc.NumExports = 3;
        dxilLibDesc.pExports = exportDesc.data();
        subobjects[index++] = stateSubobject;

        std::wstring exportName(kHitGroup);
        D3D12_HIT_GROUP_DESC hitDesc = {};
        D3D12_STATE_SUBOBJECT hitSubObject;
        hitDesc.AnyHitShaderImport = nullptr;
        hitDesc.ClosestHitShaderImport = kClosestHitShader;
        hitDesc.HitGroupExport = exportName.c_str();
        hitSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
        hitSubObject.pDesc = &hitDesc;
        subobjects[index++] = hitSubObject;  // 1 Hit Group

        // Bind the payload size to the programs
        D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
        D3D12_STATE_SUBOBJECT shaderSubobject = {};
        shaderConfig.MaxAttributeSizeInBytes = sizeof(float) * 2;
        shaderConfig.MaxPayloadSizeInBytes = sizeof(float) * 3;
        shaderSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        shaderSubobject.pDesc = &shaderConfig;
        subobjects[index] = shaderSubobject;  // 6 Shader Config

        D3D12_STATE_SUBOBJECT assocShaderConfigSubObject = {};
        D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION assocShaderConfig = {};

        uint32_t shaderConfigIndex = index++;  // 6
        const WCHAR* shaderExports[] = {kMissShader, kClosestHitShader, kRayGenShader};
        assocShaderConfig.NumExports = 3;
        assocShaderConfig.pExports = shaderExports;
        assocShaderConfig.pSubobjectToAssociate = &(subobjects[shaderConfigIndex]);
        assocShaderConfigSubObject.Type =
            D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
        assocShaderConfigSubObject.pDesc = &assocShaderConfig;
        subobjects[index++] = assocShaderConfigSubObject;

        // Create the pipeline config
        D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
        D3D12_STATE_SUBOBJECT pipelineSubobject = {};
        pipelineConfig.MaxTraceRecursionDepth = 1;
        pipelineSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
        pipelineSubobject.pDesc = &pipelineConfig;
        subobjects[index++] = pipelineSubobject;

        // Create the state
        D3D12_STATE_OBJECT_DESC desc;
        desc.NumSubobjects = index;  // 10
        desc.pSubobjects = subobjects.data();
        desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

        printf("000\n");
        DAWN_TRY(CheckHRESULT(
            device->GetD3D12Device5()->CreateStateObject(&desc, IID_PPV_ARGS(&mPipelineState)),
            "Create RT pipeline"));
        printf("111\n");

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

    ComPtr<ID3D12PipelineState> RayTracingPipeline::GetPipelineState() {
        return mPipelineState;
    }

}}  // namespace dawn_native::d3d12
