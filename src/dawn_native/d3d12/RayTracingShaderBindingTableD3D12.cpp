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

#include "dawn_native/d3d12/RayTracingShaderBindingTableD3D12.h"

#include "common/Math.h"
#include "dawn_native/Error.h"
#include "dawn_native/d3d12/AdapterD3D12.h"
#include "dawn_native/d3d12/D3D12Error.h"
#include "dawn_native/d3d12/DeviceD3D12.h"
#include "dawn_native/d3d12/ShaderVisibleDescriptorAllocatorD3D12.h"

namespace dawn_native { namespace d3d12 {

    namespace {

    }  // anonymous namespace

    // static
    ResultOrError<RayTracingShaderBindingTable*> RayTracingShaderBindingTable::Create(
        Device* device,
        const RayTracingShaderBindingTableDescriptor* descriptor) {
        std::unique_ptr<RayTracingShaderBindingTable> sbt =
            std::make_unique<RayTracingShaderBindingTable>(device, descriptor);
        DAWN_TRY(sbt->Initialize(descriptor));
        return sbt.release();
    }

    void RayTracingShaderBindingTable::DestroyImpl() {
    }

    MaybeError RayTracingShaderBindingTable::Initialize(
        const RayTracingShaderBindingTableDescriptor* descriptor) {
        for (unsigned int ii = 0; ii < descriptor->stagesCount; ++ii) {
            const RayTracingShaderBindingTableStagesDescriptor& stage = descriptor->stages[ii];
            mStages.push_back(stage);
        }
        for (unsigned int ii = 0; ii < descriptor->groupsCount; ++ii) {
            const RayTracingShaderBindingTableGroupsDescriptor& group = descriptor->groups[ii];
            mGroups.push_back(group);
        }
        return {};
    }

    RayTracingShaderBindingTable::~RayTracingShaderBindingTable() {
        DestroyInternal();
    }

    uint32_t RayTracingShaderBindingTable::GetTableSize() const {
        return mTableSize;
    }

    ComPtr<ID3D12Resource> RayTracingShaderBindingTable::GetTableBuffer() {
        return mTableBuffer;
    }

    MaybeError RayTracingShaderBindingTable::Generate(RayTracingPipeline* pipeline,
                                                      PipelineLayout* pipelineLayout) {
        Device* device = ToBackend(GetDevice());

        ID3D12StateObject* pipelineState = pipeline->GetPipelineState().Get();

        ComPtr<ID3D12StateObjectProperties> pipelineInfo;
        DAWN_TRY(CheckHRESULT(pipelineState->QueryInterface(IID_PPV_ARGS(&pipelineInfo)),
                              "Query RT pipeline info"));

        uint32_t shaderTableEntrySize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        shaderTableEntrySize += 8;  // The ray-gen's descriptor table
        shaderTableEntrySize =
            Align(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, shaderTableEntrySize);

        mTableSize = shaderTableEntrySize * 3;

        D3D12_RESOURCE_DESC resourceDescriptor;
        resourceDescriptor.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDescriptor.Alignment = 0;
        resourceDescriptor.Width = mTableSize;
        resourceDescriptor.Height = 1;
        resourceDescriptor.DepthOrArraySize = 1;
        resourceDescriptor.MipLevels = 1;
        resourceDescriptor.Format = DXGI_FORMAT_UNKNOWN;
        resourceDescriptor.SampleDesc.Count = 1;
        resourceDescriptor.SampleDesc.Quality = 0;
        resourceDescriptor.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDescriptor.Flags = D3D12_RESOURCE_FLAG_NONE;

        DAWN_TRY_ASSIGN(mTableResource,
                        ToBackend(GetDevice())
                            ->AllocateMemory(D3D12_HEAP_TYPE_UPLOAD, resourceDescriptor,
                                             D3D12_RESOURCE_STATE_GENERIC_READ));
        mTableBuffer = mTableResource.GetD3D12Resource();

        // Map the SBT
        uint8_t* pData;
        DAWN_TRY(CheckHRESULT(mTableBuffer->Map(0, nullptr, (void**)&pData), "Map SBT"));

        // Write Entry 0: ray generation id
        memcpy(pData, pipelineInfo->GetShaderIdentifier(L"rgen_main"),
               D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        // Write descriptor heap offset
        ID3D12DescriptorHeap* descriptorHeap =
            device->GetViewShaderVisibleDescriptorAllocator()->GetShaderVisibleHeap();
        uint64_t heapStart = descriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr;
        *(uint64_t*)(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) = heapStart;

        // Write Entry 1: miss id
        memcpy(pData + mTableSize, pipelineInfo->GetShaderIdentifier(L"rmiss_main"),
               D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

        // Write Entry 2: hit group id
        uint8_t* pHitEntry = pData + mTableSize * 2;  // +2 skips the ray-gen and miss entries
        memcpy(pHitEntry, pipelineInfo->GetShaderIdentifier(L"HitGroup_0"),
               D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

        // Unmap the SBT
        mTableBuffer->Unmap(0, nullptr);
        return {};
    }

    std::vector<RayTracingShaderBindingTableStagesDescriptor>&
    RayTracingShaderBindingTable::GetStages() {
        return mStages;
    }

    std::vector<RayTracingShaderBindingTableGroupsDescriptor>&
    RayTracingShaderBindingTable::GetGroups() {
        return mGroups;
    }

}}  // namespace dawn_native::d3d12
